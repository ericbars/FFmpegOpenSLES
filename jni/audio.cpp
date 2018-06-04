#include "player.h"

#define DECODE_AUDIO_BUFFER_SIZE ((AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) )

static AVFilterContext *in_audio_filter;  // the first filter in the audio chain
static AVFilterContext *out_audio_filter;  // the last filter in the audio chain
static AVFilterGraph *agraph;              // audio filter graph
static struct AudioParams audio_filter_src;

static int init_filter_graph(AVFilterGraph **graph, AVFilterContext **src,
		AVFilterContext **sink) {
	AVFilterGraph *filter_graph;
	AVFilterContext *abuffer_ctx;
	AVFilter *abuffer;
	AVFilterContext *aformat_ctx;
	AVFilter *aformat;
	AVFilterContext *abuffersink_ctx;
	AVFilter *abuffersink;

	char options_str[1024];
	char ch_layout[64];

	int err;

	/* Create a new filtergraph, which will contain all the filters. */
	filter_graph = avfilter_graph_alloc();
	if (!filter_graph) {
		av_log(NULL, AV_LOG_ERROR, "Unable to create filter graph.\n");
		return AVERROR(ENOMEM);
	}

	/* Create the abuffer filter;
	 * it will be used for feeding the data into the graph. */
	abuffer = avfilter_get_by_name("abuffer");
	if (!abuffer) {
		av_log(NULL, AV_LOG_ERROR, "Could not find the abuffer filter.\n");
		return AVERROR_FILTER_NOT_FOUND ;
	}

	abuffer_ctx = avfilter_graph_alloc_filter(filter_graph, abuffer, "src");
	if (!abuffer_ctx) {
		av_log(NULL, AV_LOG_ERROR,
				"Could not allocate the abuffer instance.\n");
		return AVERROR(ENOMEM);
	}

	/* Set the filter options through the AVOptions API. */
	av_get_channel_layout_string(ch_layout, sizeof(ch_layout), (int) 0,
			audio_filter_src.channel_layout);
	av_opt_set(abuffer_ctx, "channel_layout", ch_layout,
			AV_OPT_SEARCH_CHILDREN);
	av_opt_set(abuffer_ctx, "sample_fmt",
			av_get_sample_fmt_name(audio_filter_src.fmt),
			AV_OPT_SEARCH_CHILDREN);
	av_opt_set_q(abuffer_ctx, "time_base",
			(AVRational ) { 1, audio_filter_src.freq },
			AV_OPT_SEARCH_CHILDREN);
	av_opt_set_int(abuffer_ctx, "sample_rate", audio_filter_src.freq,
			AV_OPT_SEARCH_CHILDREN);

	/* Now initialize the filter; we pass NULL options, since we have already
	 * set all the options above. */
	err = avfilter_init_str(abuffer_ctx, NULL);
	if (err < 0) {
		av_log(NULL, AV_LOG_ERROR,
				"Could not initialize the abuffer filter.\n");
		return err;
	}

	/* Create the aformat filter;
	 * it ensures that the output is of the format we want. */
	aformat = avfilter_get_by_name("aformat");
	if (!aformat) {
		av_log(NULL, AV_LOG_ERROR, "Could not find the aformat filter.\n");
		return AVERROR_FILTER_NOT_FOUND ;
	}

	aformat_ctx = avfilter_graph_alloc_filter(filter_graph, aformat, "aformat");
	if (!aformat_ctx) {
		av_log(NULL, AV_LOG_ERROR,
				"Could not allocate the aformat instance.\n");
		return AVERROR(ENOMEM);
	}

	/* A third way of passing the options is in a string of the form
	 * key1=value1:key2=value2.... */
	snprintf(options_str, sizeof(options_str),
			"sample_fmts=%s:sample_rates=%d:channel_layouts=0x%x",
			av_get_sample_fmt_name(AV_SAMPLE_FMT_S16), audio_filter_src.freq,
			audio_filter_src.channel_layout);
	err = avfilter_init_str(aformat_ctx, options_str);
	if (err < 0) {
		av_log(NULL, AV_LOG_ERROR,
				"Could not initialize the aformat filter.\n");
		return err;
	}

	/* Finally create the abuffersink filter;
	 * it will be used to get the filtered data out of the graph. */
	abuffersink = avfilter_get_by_name("abuffersink");
	if (!abuffersink) {
		av_log(NULL, AV_LOG_ERROR, "Could not find the abuffersink filter.\n");
		return AVERROR_FILTER_NOT_FOUND ;
	}

	abuffersink_ctx = avfilter_graph_alloc_filter(filter_graph, abuffersink,
			"sink");
	if (!abuffersink_ctx) {
		av_log(NULL, AV_LOG_ERROR,
				"Could not allocate the abuffersink instance.\n");
		return AVERROR(ENOMEM);
	}

	/* This filter takes no options. */
	err = avfilter_init_str(abuffersink_ctx, NULL);
	if (err < 0) {
		av_log(NULL, AV_LOG_ERROR,
				"Could not initialize the abuffersink instance.\n");
		return err;
	}

	/* Connect the filters;
	 * in this simple case the filters just form a linear chain. */
	err = avfilter_link(abuffer_ctx, 0, aformat_ctx, 0);
	if (err >= 0) {
		err = avfilter_link(aformat_ctx, 0, abuffersink_ctx, 0);
	}

	if (err < 0) {
		av_log(NULL, AV_LOG_ERROR, "Error connecting filters\n");
		return err;
	}

	/* Configure the graph. */
	err = avfilter_graph_config(filter_graph, NULL);
	if (err < 0) {
		av_log(NULL, AV_LOG_ERROR, "Error configuring the filter graph\n");
		return err;
	}

	*graph = filter_graph;
	*src = abuffer_ctx;
	*sink = abuffersink_ctx;

	return 0;
}

static inline int64_t get_valid_channel_layout(int64_t channel_layout,
		int channels) {
	if (channel_layout
			&& av_get_channel_layout_nb_channels(channel_layout) == channels) {
		return channel_layout;
	} else {
		return 0;
	}
}

// decode a new packet(not multi-frame)
// return decoded frame size, not decoded packet size
int audio_decode_frame(uint8_t *audio_buf, int buf_size) {
	static AVPacket pkt;
	static uint8_t *audio_pkt_data = NULL;
	static int audio_pkt_size = 0;
	int len1, data_size;
	int got_frame;
	AVFrame * frame = NULL;
	static int reconfigure = 1;
	int ret = -1;

	for (;;) {

		while (audio_pkt_size > 0) {

			if (NULL == frame) {
				frame = av_frame_alloc();
			}

			data_size = buf_size;
			got_frame = 0;

			// len1 is decoded packet size
			len1 = avcodec_decode_audio4(global_context.acodec_ctx, frame,
					&got_frame, &pkt);
			if (got_frame) {

				if (reconfigure) {

					reconfigure = 0;
					int64_t dec_channel_layout = get_valid_channel_layout(
							frame->channel_layout,
							av_frame_get_channels(frame));

					// used by init_filter_graph()
					audio_filter_src.fmt = (enum AVSampleFormat) frame->format;
					audio_filter_src.channels = av_frame_get_channels(frame);
					audio_filter_src.channel_layout = dec_channel_layout;
					audio_filter_src.freq = frame->sample_rate;

					init_filter_graph(&agraph, &in_audio_filter,
							&out_audio_filter);
				}

				if ((ret = av_buffersrc_add_frame(in_audio_filter, frame))
						< 0) {
					av_log(NULL, AV_LOG_ERROR,
							"av_buffersrc_add_frame :  failure. \n");
					return ret;
				}

				if ((ret = av_buffersink_get_frame(out_audio_filter, frame))
						< 0) {
					av_log(NULL, AV_LOG_ERROR,
							"av_buffersink_get_frame :  failure. \n");
					continue;
				}

				data_size = av_samples_get_buffer_size(NULL, frame->channels,
						frame->nb_samples, (enum AVSampleFormat) frame->format,
						1);

				// len1 is decoded packet size
				// < 0  means failure or error，so break to get a new packet
				if (len1 < 0) {
					audio_pkt_size = 0;
					av_log(NULL, AV_LOG_ERROR,
							"avcodec_decode_audio4 failure. \n");
					break;
				}

				// decoded data to audio buf
				memcpy(audio_buf, frame->data[0], data_size);

				audio_pkt_data += len1;
				audio_pkt_size -= len1;

				int n = 2 * global_context.acodec_ctx->channels;
				/*audio_clock += (double) data_size
				 / (double) (n * global_context.acodec_ctx->sample_rate); // add bytes offset */
				av_free_packet(&pkt);
				av_frame_free(&frame);

				return data_size;
			} else if (len1 < 0) {
				char errbuf[64];
				av_strerror(ret, errbuf, 64);
				LOGV2("avcodec_decode_audio4 ret < 0, %s", errbuf);
			}
		}

		av_free_packet(&pkt);
		av_frame_free(&frame);

		// get a new packet
		if (packet_queue_get(&global_context.audio_queue, &pkt) < 0) {
			return -1;
		}

		//LOGV2("pkt.size is %d", pkt.size);

		audio_pkt_data = pkt.data;
		audio_pkt_size = pkt.size;
	}

	return ret;
}

