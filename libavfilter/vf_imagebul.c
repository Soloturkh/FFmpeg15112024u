// vf_imagebul.c - FFmpeg özel filtresi, OpenCV ile şablon eşleştirme yaparak frameleri atlar
#include <opencv2/opencv.hpp>
#include "libavfilter/avfilter.h"
#include "libavfilter/avfilterinternal.h"
#include "libavfilter/buffersink.h"
#include "libavfilter/buffersrc.h"
#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"

typedef struct ImageBulFilterContext {
    const AVClass *class;
    std::vector<cv::Mat> templates; // Şablon görselleri
    double match_threshold;         // Eşik değeri
    char *template_paths;           // Şablon görsellerin yolları
} ImageBulFilterContext;

// Şablon resimlerini yükleme fonksiyonu
static int load_templates(ImageBulFilterContext *s) {
    // template_paths virgülle ayrılmış bir dizi olarak kabul ediliyor
    char *token = strtok(s->template_paths, ",");
    while (token) {
        cv::Mat templ = cv::imread(token, cv::IMREAD_GRAYSCALE);
        if (templ.empty()) {
            av_log(NULL, AV_LOG_ERROR, "Şablon resmi yüklenemedi: %s\n", token);
            return AVERROR(EINVAL);
        }
        s->templates.push_back(templ);
        token = strtok(NULL, ",");
    }
    return 0;
}

// Filtreyi başlatma fonksiyonu
static int init(AVFilterContext *ctx) {
    ImageBulFilterContext *s = (ImageBulFilterContext *)ctx->priv;
    if (load_templates(s) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Şablonlar yüklenemedi.\n");
        return AVERROR(EINVAL);
    }
    return 0;
}

// Frame işlemeyi yapan filtre fonksiyonu
static int filter_frame(AVFilterLink *inlink, AVFrame *frame) {
    ImageBulFilterContext *s = (ImageBulFilterContext *)inlink->dst->priv;

    // Frame'i OpenCV mat formatına dönüştür
    cv::Mat img(frame->height, frame->width, CV_8UC3, frame->data[0], frame->linesize[0]);
    cv::Mat gray;
    cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);

    // Her bir şablon için şablon eşleştirme yap
    for (const auto& templ : s->templates) {
        cv::Mat result;
        cv::matchTemplate(gray, templ, result, cv::TM_CCOEFF_NORMED);

        // Sonuçlardan en yüksek benzerlik skorunu bul
        double minVal, maxVal;
        cv::minMaxLoc(result, &minVal, &maxVal);

        // Eğer eşleşme eşiğin üzerindeyse frame'i atlayın
        if (maxVal >= s->match_threshold) {
            av_frame_unref(frame);  // Frame'i serbest bırak
            return AVERROR(EAGAIN); // FFmpeg'e frame'in atlanması gerektiğini bildir
        }
    }

    // Hiçbir şablona eşleşme yoksa frame'i işleme devam et
    return ff_filter_frame(inlink->dst->outputs[0], frame);
}

// Filtreyi kapatma fonksiyonu
static av_cold void uninit(AVFilterContext *ctx) {
    ImageBulFilterContext *s = (ImageBulFilterContext *)ctx->priv;
}

// Filtre açıklaması ve ayarları
#define OFFSET(x) offsetof(ImageBulFilterContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption imagebul_options[] = {
    { "threshold", "Eşik benzerlik oranı (0.0 - 1.0 arası)", OFFSET(match_threshold), AV_OPT_TYPE_DOUBLE, {.dbl=0.7}, 0.0, 1.0, FLAGS },
    { "templates", "Virgülle ayrılmış şablon resim yolları", OFFSET(template_paths), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(imagebul);

static const AVFilterPad avfilter_vf_imagebul_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad avfilter_vf_imagebul_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_imagebul = {
    .name          = "imagebul",
    .description   = NULL_IF_CONFIG_SMALL("Şablon eşleşmeye göre frameleri atlayan özel filtre"),
    .priv_size     = sizeof(ImageBulFilterContext),
    .init          = init,
    .uninit        = uninit,
    .inputs        = avfilter_vf_imagebul_inputs,
    .outputs       = avfilter_vf_imagebul_outputs,
    .priv_class    = &imagebul_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
