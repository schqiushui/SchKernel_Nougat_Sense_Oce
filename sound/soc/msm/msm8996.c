/*
 * Copyright (c) 2014-2016, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/switch.h>
#include <linux/input.h>
#include <sound/core.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/pcm.h>
#include <sound/jack.h>
#include <sound/q6afe-v2.h>
#include <sound/q6core.h>
#include <sound/pcm_params.h>
#include <sound/info.h>
#include "device_event.h"
#include "qdsp6v2/msm-pcm-routing-v2.h"
#include "../codecs/wcd9xxx-common.h"
#include "../codecs/wcd9330.h"
#include "../codecs/wcd9335.h"
#ifdef USE_WSA_AMP
#include "../codecs/wsa881x.h"
#endif

#include <sound/htc_acoustic_alsa.h>
#include <linux/qdsp6v2/apr.h>
#ifdef CONFIG_USE_AS_HS
#include <linux/qpnp/qpnp-adc.h>
#include <linux/qdsp6v2/apr.h>
#endif

#define	ADAP_CONF_FILE_SIZE 4096
#define	ADAP_ONE_CHANNEL_SIZE (ADAP_CONF_FILE_SIZE/2)

#define	ADAP_ONE_CHANNEL_CONF_NUM          (ADAP_ONE_CHANNEL_SIZE/sizeof(short))
#define	ADAP_ONE_CHANNEL_CONF_NUM_BY_DWORD (ADAP_ONE_CHANNEL_SIZE/sizeof(int32_t))

int32_t g_AdapConfLchannel[ADAP_ONE_CHANNEL_CONF_NUM_BY_DWORD] = {0};
int32_t g_AdapConfRchannel[ADAP_ONE_CHANNEL_CONF_NUM_BY_DWORD] = {0};

char g_AdapConfFilePath[128]={0}, g_AdapGainFilePath[128]={0};;
int g_gain = 0;

#define DRV_NAME "msm8996-asoc-snd"

#define SAMPLING_RATE_8KHZ      8000
#define SAMPLING_RATE_16KHZ     16000
#define SAMPLING_RATE_32KHZ     32000
#define SAMPLING_RATE_48KHZ     48000
#define SAMPLING_RATE_96KHZ     96000
#define SAMPLING_RATE_192KHZ    192000
#define SAMPLING_RATE_44P1KHZ   44100

#define MSM8996_SPK_ON     1
#define MSM8996_HIFI_ON    1

#define WCD9XXX_MBHC_DEF_BUTTONS    8
#define WCD9XXX_MBHC_DEF_RLOADS     5
#define CODEC_EXT_CLK_RATE         9600000
#define ADSP_STATE_READY_TIMEOUT_MS    3000
#define DEV_NAME_STR_LEN            32

#ifdef USE_WSA_AMP
#define WSA8810_NAME_1 "wsa881x.20170211"
#define WSA8810_NAME_2 "wsa881x.20170212"
#endif

#undef pr_debug
#undef pr_info
#undef pr_err
#define pr_debug(fmt, ...) pr_aud_debug(fmt, ##__VA_ARGS__)
#define pr_info(fmt, ...) pr_aud_info(fmt, ##__VA_ARGS__)
#define pr_err(fmt, ...) pr_aud_err(fmt, ##__VA_ARGS__)

#ifdef USE_WSA_AMP
enum {
	AUX_DEV_NONE = 0,
	WSA8810,
	WSA8815,
};
#endif

static int slim0_rx_sample_rate = SAMPLING_RATE_48KHZ;
static int slim0_tx_sample_rate = SAMPLING_RATE_48KHZ;
static int slim1_tx_sample_rate = SAMPLING_RATE_48KHZ;
static int slim0_rx_bit_format = SNDRV_PCM_FORMAT_S24_LE; 
static int slim0_tx_bit_format = SNDRV_PCM_FORMAT_S16_LE;
static int slim1_tx_bit_format = SNDRV_PCM_FORMAT_S16_LE;
static int hdmi_rx_bit_format = SNDRV_PCM_FORMAT_S16_LE;
static int msm8996_auxpcm_rate = SAMPLING_RATE_8KHZ;
static int slim5_rx_sample_rate = SAMPLING_RATE_48KHZ;
static int slim5_rx_bit_format = SNDRV_PCM_FORMAT_S16_LE;
static int slim6_rx_sample_rate = SAMPLING_RATE_48KHZ;
static int slim6_rx_bit_format = SNDRV_PCM_FORMAT_S16_LE;

static struct platform_device *spdev;
static int ext_us_amp_gpio = -1;
static int msm8996_spk_control = 1;
static int msm_slim_0_rx_ch = 1;
static int msm_slim_0_tx_ch = 1;
static int msm_slim_1_tx_ch = 1;
static int msm_slim_5_rx_ch = 1;
static int msm_slim_6_rx_ch = 1;
static int msm_hifi_control;
static int msm_vi_feed_tx_ch = 2;

static int msm_hdmi_rx_ch = 2;
static int msm_proxy_rx_ch = 2;
static int hdmi_rx_sample_rate = SAMPLING_RATE_48KHZ;

static bool codec_reg_done;

static const char *const htc_ftm_mode[] = {"Default", "FTM"};
static const char *const htc_as20_vol_index[] = {"None", "Zero", "One", "Two", "Three", "Four",
						"Five", "Six", "Seven", "Eight", "Nine",
						"Ten", "Eleven", "Twelve", "Thirteen", "Fourteen", "Fifteen"};
static const char *const hifi_function[] = {"Off", "On"};
static const char *const pin_states[] = {"Disable", "active"};
static const char *const spk_function[] = {"Off", "On"};
static const char *const slim0_rx_ch_text[] = {"One", "Two"};
static const char *const slim5_rx_ch_text[] = {"One", "Two"};
static const char *const slim6_rx_ch_text[] = {"One", "Two"};
static const char *const slim0_tx_ch_text[] = {"One", "Two", "Three", "Four",
						"Five", "Six", "Seven",
						"Eight"};
static const char *const vi_feed_ch_text[] = {"One", "Two"};
static char const *hdmi_rx_ch_text[] = {"Two", "Three", "Four", "Five",
					"Six", "Seven", "Eight"};
static char const *rx_bit_format_text[] = {"S16_LE", "S24_LE", "S24_3LE"};
static char const *slim5_rx_bit_format_text[] = {"S16_LE", "S24_LE", "S24_3LE"};
static char const *slim6_rx_bit_format_text[] = {"S16_LE", "S24_LE", "S24_3LE"};
static char const *slim0_rx_sample_rate_text[] = {"KHZ_48", "KHZ_96",
					"KHZ_192", "KHZ_44P1", "KHZ_8",
					"KHZ_16", "KHZ_32"};
static char const *slim5_rx_sample_rate_text[] = {"KHZ_48", "KHZ_96",
						  "KHZ_192", "KHZ_44P1"};
static char const *slim6_rx_sample_rate_text[] = {"KHZ_48", "KHZ_96",
						  "KHZ_192", "KHZ_44P1"};
static const char *const proxy_rx_ch_text[] = {"One", "Two", "Three", "Four",
	"Five", "Six", "Seven", "Eight"};

static char const *hdmi_rx_sample_rate_text[] = {"KHZ_48", "KHZ_96",
					"KHZ_192"};

static const char *const auxpcm_rate_text[] = {"8000", "16000"};
static const struct soc_enum msm8996_auxpcm_enum[] = {
		SOC_ENUM_SINGLE_EXT(2, auxpcm_rate_text),
};

static struct afe_clk_set mi2s_tx_clk = {
	AFE_API_VERSION_I2S_CONFIG,
	Q6AFE_LPASS_CLK_ID_TER_MI2S_IBIT,
	Q6AFE_LPASS_IBIT_CLK_1_P536_MHZ,
	Q6AFE_LPASS_CLK_ATTRIBUTE_COUPLE_NO,
	Q6AFE_LPASS_CLK_ROOT_DEFAULT,
	0,
};

static int pri_mi2s_sample_rate = SAMPLING_RATE_48KHZ;
static int sec_mi2s_sample_rate = SAMPLING_RATE_48KHZ;
static int tert_mi2s_sample_rate = SAMPLING_RATE_48KHZ;
static int quat_mi2s_sample_rate = SAMPLING_RATE_48KHZ;

static int pri_mi2s_bit_format = SNDRV_PCM_FORMAT_S16_LE;
static int sec_mi2s_bit_format = SNDRV_PCM_FORMAT_S24_LE; 
static int tert_mi2s_bit_format = SNDRV_PCM_FORMAT_S16_LE;
static int quat_mi2s_bit_format = SNDRV_PCM_FORMAT_S24_LE;

static int msm_pri_mi2s_tx_ch = 2;
static int msm_pri_mi2s_rx_ch = 2;
static int msm_sec_mi2s_tx_ch = 2;
static int msm_sec_mi2s_rx_ch = 2; 
static int msm_tert_mi2s_tx_ch = 2;
static int msm_tert_mi2s_rx_ch = 2;
static int msm_quat_mi2s_tx_ch = 2;
static int msm_quat_mi2s_rx_ch = 2;

struct htc_request_gpio {
	unsigned gpio_no;
	const char* gpio_name;
};

static struct aud_btpcm_config {
	int init;
	int ftm;
	struct htc_request_gpio gpio[4];
} htc_aud_btpcm_config = {
	.init = 0,
	.ftm = 0,
	.gpio = {
		{ .gpio_name = "ftm-btpcm-dout",},	
		{ .gpio_name = "ftm-btpcm-din",},	
		{ .gpio_name = "ftm-btpcm-clock",},	
		{ .gpio_name = "ftm-btpcm-sync",},	
	},
};

static struct mutex htc_adaptivesound_enable_mutex;
int htc_adaptivesound_enable = 0;
int htc_onedotone_enable = 0;
int htc_as20_volume_index = 0;

struct msm_mi2s_pdata {
	u16 rx_sd_lines;
	u16 tx_sd_lines;
	u16 intf_id;
	u16 slave;
	u32 ext_mclk_rate;
};

struct msm_mi2s_data {
	struct afe_clk_set mi2s_clk;
	struct afe_clk_set mi2s_mclk;
	atomic_t mi2s_rsc_ref;
	int * sample_rate;
	int * bit_format;
};

static struct msm_mi2s_data msm_pri_mi2s_data = {
	.mi2s_clk = {
		AFE_API_VERSION_I2S_CONFIG,
		Q6AFE_LPASS_CLK_ID_PRI_MI2S_IBIT,
		Q6AFE_LPASS_IBIT_CLK_1_P536_MHZ,
		Q6AFE_LPASS_CLK_ATTRIBUTE_COUPLE_NO,
		Q6AFE_LPASS_CLK_ROOT_DEFAULT,
		0,
	},
	.sample_rate = &pri_mi2s_sample_rate,
	.bit_format = &pri_mi2s_bit_format,
};

static struct msm_mi2s_data msm_sec_mi2s_data = {
	.mi2s_clk = {
		AFE_API_VERSION_I2S_CONFIG,
		Q6AFE_LPASS_CLK_ID_SEC_MI2S_IBIT,
		Q6AFE_LPASS_IBIT_CLK_1_P536_MHZ,
		Q6AFE_LPASS_CLK_ATTRIBUTE_COUPLE_NO,
		Q6AFE_LPASS_CLK_ROOT_DEFAULT,
		0,
	},
	.mi2s_mclk = {
		AFE_API_VERSION_I2S_CONFIG,
		Q6AFE_LPASS_CLK_ID_MCLK_2, 
		0,
		Q6AFE_LPASS_CLK_ATTRIBUTE_COUPLE_NO,
		Q6AFE_LPASS_CLK_ROOT_DEFAULT,
		0,
	},
	.sample_rate = &sec_mi2s_sample_rate,
	.bit_format = &sec_mi2s_bit_format,
};

static struct msm_mi2s_data msm_tert_mi2s_data = {
	.mi2s_clk = {
		AFE_API_VERSION_I2S_CONFIG,
		Q6AFE_LPASS_CLK_ID_TER_MI2S_IBIT,
		Q6AFE_LPASS_IBIT_CLK_1_P536_MHZ,
		Q6AFE_LPASS_CLK_ATTRIBUTE_COUPLE_NO,
		Q6AFE_LPASS_CLK_ROOT_DEFAULT,
		0,
	},
	.sample_rate = &tert_mi2s_sample_rate,
	.bit_format = &tert_mi2s_bit_format,
};

static struct msm_mi2s_data msm_quat_mi2s_data = {
	.mi2s_clk = {
		AFE_API_VERSION_I2S_CONFIG,
		Q6AFE_LPASS_CLK_ID_QUAD_MI2S_IBIT,
		Q6AFE_LPASS_IBIT_CLK_1_P536_MHZ,
		Q6AFE_LPASS_CLK_ATTRIBUTE_COUPLE_NO,
		Q6AFE_LPASS_CLK_ROOT_DEFAULT,
		0,
	},
	.sample_rate = &quat_mi2s_sample_rate,
	.bit_format = &quat_mi2s_bit_format,
};

static char const *mi2s_bit_format_text[] = {"S16_LE", "S24_LE"};
static char const *mi2s_sample_rate_text[] = {"KHZ_8", "KHZ_16", "KHZ_32",
						"KHZ_48", "KHZ_96", "KHZ_192"};

static const char *const pri_mi2s_tx_ch_text[] = {"One", "Two", "Three",
						  "Four"};
static const char *const pri_mi2s_rx_ch_text[] = {"One", "Two", "Three",
						  "Four"};
static const char *const sec_mi2s_tx_ch_text[] = {"One", "Two", "Three",
						  "Four"};
static const char *const sec_mi2s_rx_ch_text[] = {"One", "Two", "Three",
						  "Four"};
static const char *const tert_mi2s_tx_ch_text[] = {"One", "Two", "Three",
						   "Four"};
static const char *const tert_mi2s_rx_ch_text[] = {"One", "Two", "Three",
						   "Four"};
static const char *const quat_mi2s_tx_ch_text[] = {"One", "Two", "Three", "Four",
						   "Five", "Six", "Seven",
						   "Eight"};
static const char *const quat_mi2s_rx_ch_text[] = {"One", "Two", "Three", "Four",
						   "Five", "Six", "Seven",
						   "Eight"};

#ifdef USE_WSA_AMP
struct msm8996_wsa881x_dev_info {
	struct device_node *of_node;
	u32 index;
};

static struct snd_soc_aux_dev *msm8996_aux_dev;
static struct snd_soc_codec_conf *msm8996_codec_conf;
#endif

struct msm8996_asoc_mach_data {
	u32 mclk_freq;
	int us_euro_gpio;
	int hph_en1_gpio;
	int hph_en0_gpio;
	struct snd_info_entry *codec_root;
#ifdef CONFIG_RT_REGMAP
	int audio_1v8_hph_en_gpio;
	int rt5503_reset_gpio;
#endif
};

struct msm8996_asoc_wcd93xx_codec {
	void* (*get_afe_config_fn)(struct snd_soc_codec *codec,
				   enum afe_config_type config_type);
#ifdef CONFIG_USE_CODEC_MBHC
	void (*mbhc_hs_detect_exit)(struct snd_soc_codec *codec);
#endif
};

static struct msm8996_asoc_wcd93xx_codec msm8996_codec_fn;

struct msm8996_liquid_dock_dev {
	int dock_plug_gpio;
	int dock_plug_irq;
	int dock_plug_det;
	struct work_struct irq_work;
	struct switch_dev audio_sdev;
};
static struct msm8996_liquid_dock_dev *msm8996_liquid_dock_dev;

static void *adsp_state_notifier;
#ifdef CONFIG_USE_CODEC_MBHC
static void *def_tasha_mbhc_cal(void);
#endif
static int msm_snd_enable_codec_ext_clk(struct snd_soc_codec *codec,
					int enable, bool dapm);
#ifdef USE_WSA_AMP
static int msm8996_wsa881x_init(struct snd_soc_component *component);
#endif

#ifdef CONFIG_USE_CODEC_MBHC
static struct wcd_mbhc_config wcd_mbhc_cfg = {
	.read_fw_bin = false,
	.calibration = NULL,
	.detect_extn_cable = true,
	.mono_stero_detection = false,
	.swap_gnd_mic = NULL,
	.hs_ext_micbias = true,
#if 0
	.key_code[0] = KEY_MEDIA,
	.key_code[1] = KEY_VOICECOMMAND,
	.key_code[2] = KEY_VOLUMEUP,
	.key_code[3] = KEY_VOLUMEDOWN,
#else
	.key_code[0] = KEY_MEDIA,
	.key_code[1] = KEY_VOLUMEUP,
	.key_code[2] = KEY_VOLUMEDOWN,
	.key_code[3] = 0,
#endif
	.key_code[4] = 0,
	.key_code[5] = 0,
	.key_code[6] = 0,
	.key_code[7] = 0,
	.linein_th = 5000,
	.moist_cfg = { V_45_MV, I_3P0_UA },
	.mbhc_micbias = MIC_BIAS_2,
#ifdef CONFIG_USE_AS_HS
	.htc_headset_cfg.htc_headset_init = false,
#endif
};
#endif

#define CARD_TIMEOUT 30000 
static struct delayed_work card_det_work;
static void htc_card_det(struct work_struct *work);
static DECLARE_DELAYED_WORK(card_det_work, htc_card_det);
static int card_reg = -1;

#ifdef CONFIG_USE_AS_HS
static int hs_qpnp_remote_adc(int *adc,unsigned int channel)
{
	struct qpnp_vadc_result result;
	enum qpnp_vadc_channels chan;
	static struct qpnp_vadc_chip *vadc_chip;
	int err = 0;

	vadc_chip = qpnp_get_vadc(&spdev->dev, "headset");

	result.physical = -EINVAL;
	chan = channel;
	pr_debug("%s: pdata_chann %d\n", __func__, chan);
	err = qpnp_vadc_read(vadc_chip, chan, &result);
	if (err < 0) {
		pr_err("%s: Read ADC fail, ret = %d\n", __func__, err);
		return err;
	}

	*adc = (int) result.physical;
	*adc = *adc / 1000; 
	pr_info("%s: Remote ADC %d (%#X)\n", __func__, *adc, *adc);
	return 1;
}

static int msm_headset_lr_event(struct snd_soc_dapm_widget *w,
				 struct snd_kcontrol *k, int event)
{
	int ret = 0;
	pr_info("%s\n", __func__);
	if (SND_SOC_DAPM_EVENT_ON(event)) {
		if (gpio_get_value(wcd_mbhc_cfg.htc_headset_cfg.ext_micbias) != 1) {
			gpio_set_value(wcd_mbhc_cfg.htc_headset_cfg.ext_micbias, 1);
			pr_info("%s: ext_micbias on\n",__func__);
		}
	} else {
		if (gpio_get_value(wcd_mbhc_cfg.htc_headset_cfg.ext_micbias) != 0) {
			gpio_set_value(wcd_mbhc_cfg.htc_headset_cfg.ext_micbias, 0);
			pr_info("%s: ext_micbias off\n",__func__);
		}
	}
	return ret;
}
#endif

static inline int param_is_mask(int p)
{
	return (p >= SNDRV_PCM_HW_PARAM_FIRST_MASK) &&
			(p <= SNDRV_PCM_HW_PARAM_LAST_MASK);
}

static inline struct snd_mask *param_to_mask(struct snd_pcm_hw_params *p,
					     int n)
{
	return &(p->masks[n - SNDRV_PCM_HW_PARAM_FIRST_MASK]);
}

static void param_set_mask(struct snd_pcm_hw_params *p, int n, unsigned bit)
{
	if (bit >= SNDRV_MASK_MAX)
		return;
	if (param_is_mask(n)) {
		struct snd_mask *m = param_to_mask(p, n);
		m->bits[0] = 0;
		m->bits[1] = 0;
		m->bits[bit >> 5] |= (1 << (bit & 31));
	}
}

static void msm8996_liquid_docking_irq_work(struct work_struct *work)
{
	struct msm8996_liquid_dock_dev *dock_dev =
		container_of(work, struct msm8996_liquid_dock_dev,
			     irq_work);

	dock_dev->dock_plug_det =
		gpio_get_value(dock_dev->dock_plug_gpio);

	switch_set_state(&dock_dev->audio_sdev, dock_dev->dock_plug_det);
	
	sysfs_notify(&dock_dev->audio_sdev.dev->kobj, NULL, "state");
}

static irqreturn_t msm8996_liquid_docking_irq_handler(int irq, void *dev)
{
	struct msm8996_liquid_dock_dev *dock_dev = dev;

	
	schedule_work(&dock_dev->irq_work);
	return IRQ_HANDLED;
}

static int msm8996_liquid_init_docking(void)
{
	int ret = 0;
	int dock_plug_gpio = 0;

	
	u32 dock_plug_irq_flags = IRQF_TRIGGER_RISING |
				  IRQF_TRIGGER_FALLING |
				  IRQF_SHARED;

	dock_plug_gpio = of_get_named_gpio(spdev->dev.of_node,
					   "qcom,dock-plug-det-irq", 0);

	if (dock_plug_gpio >= 0) {
		msm8996_liquid_dock_dev =
		 kzalloc(sizeof(*msm8996_liquid_dock_dev), GFP_KERNEL);
		if (!msm8996_liquid_dock_dev) {
			pr_err("msm8996_liquid_dock_dev alloc fail.\n");
			ret = -ENOMEM;
			goto exit;
		}

		msm8996_liquid_dock_dev->dock_plug_gpio = dock_plug_gpio;

		ret = gpio_request(msm8996_liquid_dock_dev->dock_plug_gpio,
					   "dock-plug-det-irq");
		if (ret) {
			pr_err("%s:failed request msm8996_liquid_dock_plug_gpio err = %d\n",
				__func__, ret);
			ret = -EINVAL;
			goto fail_dock_gpio;
		}

		msm8996_liquid_dock_dev->dock_plug_det =
			gpio_get_value(
				msm8996_liquid_dock_dev->dock_plug_gpio);
		msm8996_liquid_dock_dev->dock_plug_irq =
			gpio_to_irq(
				msm8996_liquid_dock_dev->dock_plug_gpio);

		ret = request_irq(msm8996_liquid_dock_dev->dock_plug_irq,
				  msm8996_liquid_docking_irq_handler,
				  dock_plug_irq_flags,
				  "liquid_dock_plug_irq",
				  msm8996_liquid_dock_dev);
		if (ret < 0) {
			pr_err("%s: Request Irq Failed err = %d\n",
				__func__, ret);
			goto fail_dock_gpio;
		}

		msm8996_liquid_dock_dev->audio_sdev.name =
						QC_AUDIO_EXTERNAL_SPK_1_EVENT;

		if (switch_dev_register(
			 &msm8996_liquid_dock_dev->audio_sdev) < 0) {
			pr_err("%s: dock device register in switch diretory failed\n",
				__func__);
			goto fail_switch_dev;
		}

		INIT_WORK(
			&msm8996_liquid_dock_dev->irq_work,
			msm8996_liquid_docking_irq_work);
	}
	return 0;

fail_switch_dev:
	free_irq(msm8996_liquid_dock_dev->dock_plug_irq,
				msm8996_liquid_dock_dev);
fail_dock_gpio:
	gpio_free(msm8996_liquid_dock_dev->dock_plug_gpio);
exit:
	kfree(msm8996_liquid_dock_dev);
	msm8996_liquid_dock_dev = NULL;
	return ret;
}

static void msm8996_ext_control(struct snd_soc_codec *codec)
{
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	mutex_lock(&codec->mutex);
	pr_debug("%s: msm8996_spk_control = %d", __func__,
		 msm8996_spk_control);
	if (msm8996_spk_control == MSM8996_SPK_ON) {
		snd_soc_dapm_enable_pin(dapm, "Lineout_1 amp");
		snd_soc_dapm_enable_pin(dapm, "Lineout_2 amp");
	} else {
		snd_soc_dapm_disable_pin(dapm, "Lineout_1 amp");
		snd_soc_dapm_disable_pin(dapm, "Lineout_2 amp");
	}
	mutex_unlock(&codec->mutex);
	snd_soc_dapm_sync(dapm);
}

static int msm8996_get_spk(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s: msm8996_spk_control = %d\n",
			 __func__, msm8996_spk_control);
	ucontrol->value.integer.value[0] = msm8996_spk_control;
	return 0;
}

static int msm8996_set_spk(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);

	pr_debug("%s() ucontrol->value.integer.value[0] = %ld\n",
		 __func__, ucontrol->value.integer.value[0]);
	if (msm8996_spk_control == ucontrol->value.integer.value[0])
		return 0;

	msm8996_spk_control = ucontrol->value.integer.value[0];
	msm8996_ext_control(codec);
	return 1;
}

static int msm8996_hifi_ctrl(struct snd_soc_codec *codec)
{
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	struct snd_soc_card *card = codec->component.card;
	struct msm8996_asoc_mach_data *pdata =
				snd_soc_card_get_drvdata(card);

	pr_debug("%s: msm_hifi_control = %d", __func__,
		 msm_hifi_control);
	if (pdata->hph_en1_gpio < 0) {
		pr_err("%s: hph_en1_gpio is invalid\n", __func__);
		return -EINVAL;
	}
	mutex_lock(&codec->mutex);
	if (msm_hifi_control == MSM8996_HIFI_ON) {
		gpio_direction_output(pdata->hph_en1_gpio, 1);
		
		usleep_range(5000, 5010);
	} else {
		gpio_direction_output(pdata->hph_en1_gpio, 0);
	}
	mutex_unlock(&codec->mutex);
	snd_soc_dapm_sync(dapm);
	return 0;
}

static int msm8996_hifi_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s: msm_hifi_control = %d\n",
			 __func__, msm_hifi_control);
	ucontrol->value.integer.value[0] = msm_hifi_control;
	return 0;
}

static int msm8996_hifi_put(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);

	pr_debug("%s() ucontrol->value.integer.value[0] = %ld\n",
		 __func__, ucontrol->value.integer.value[0]);

	msm_hifi_control = ucontrol->value.integer.value[0];
	msm8996_hifi_ctrl(codec);
	return 1;
}

static int msm8996_ext_us_amp_init(void)
{
	int ret = 0;

	ext_us_amp_gpio = of_get_named_gpio(spdev->dev.of_node,
				"qcom,ext-ult-spk-amp-gpio", 0);
	if (ext_us_amp_gpio >= 0) {
		ret = gpio_request(ext_us_amp_gpio, "ext_us_amp_gpio");
		if (ret) {
			pr_err("%s: ext_us_amp_gpio request failed, ret:%d\n",
				__func__, ret);
			return ret;
		}
		gpio_direction_output(ext_us_amp_gpio, 0);
	}
	return ret;
}

static void msm8996_ext_us_amp_enable(u32 on)
{
	if (on)
		gpio_direction_output(ext_us_amp_gpio, 1);
	else
		gpio_direction_output(ext_us_amp_gpio, 0);

	pr_debug("%s: US Emitter GPIO enable:%s\n", __func__,
			on ? "Enable" : "Disable");
}

static int msm_ext_ultrasound_event(struct snd_soc_dapm_widget *w,
				    struct snd_kcontrol *k, int event)
{
	pr_debug("%s()\n", __func__);
	if (strcmp(w->name, "ultrasound amp")) {
		if (!gpio_is_valid(ext_us_amp_gpio)) {
			pr_err("%s: ext_us_amp_gpio isn't configured\n",
				__func__);
			return -EINVAL;
		}
		if (SND_SOC_DAPM_EVENT_ON(event))
			msm8996_ext_us_amp_enable(1);
		else
			msm8996_ext_us_amp_enable(0);
	} else {
		pr_err("%s() Invalid Widget = %s\n",
				__func__, w->name);
		return -EINVAL;
	}
	return 0;
}

static int msm_snd_enable_codec_ext_clk(struct snd_soc_codec *codec,
					int enable, bool dapm)
{
	if (!strcmp(dev_name(codec->dev), "tasha_codec"))
		return tasha_cdc_mclk_enable(codec, enable, dapm);
	else {
		dev_err(codec->dev, "%s: unknown codec to enable ext clk\n",
			__func__);
		return -EINVAL;
	}
}

static int msm8996_mclk_event(struct snd_soc_dapm_widget *w,
				 struct snd_kcontrol *kcontrol, int event)
{
	pr_debug("%s: event = %d\n", __func__, event);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		return msm_snd_enable_codec_ext_clk(w->codec, 1, true);
	case SND_SOC_DAPM_POST_PMD:
		return msm_snd_enable_codec_ext_clk(w->codec, 0, true);
	}
	return 0;
}

static int msm_snd_enable_codec_ext_tx_clk(struct snd_soc_codec *codec,
					   int enable, bool dapm)
{
	int ret = 0;

	if (!strcmp(dev_name(codec->dev), "tasha_codec"))
		ret = tasha_cdc_mclk_tx_enable(codec, enable, dapm);
	else {
		dev_err(codec->dev, "%s: unknown codec to enable ext clk\n",
			__func__);
		ret = -EINVAL;
	}
	return ret;
}

static int msm8996_mclk_tx_event(struct snd_soc_dapm_widget *w,
				 struct snd_kcontrol *kcontrol, int event)
{
	pr_debug("%s: event = %d\n", __func__, event);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		return msm_snd_enable_codec_ext_tx_clk(w->codec, 1, true);
	case SND_SOC_DAPM_POST_PMD:
		return msm_snd_enable_codec_ext_tx_clk(w->codec, 0, true);
	}
	return 0;
}

static int msm_hifi_ctrl_event(struct snd_soc_dapm_widget *w,
				    struct snd_kcontrol *k, int event)
{
	struct snd_soc_card *card = w->codec->component.card;
	struct msm8996_asoc_mach_data *pdata =
				snd_soc_card_get_drvdata(card);
	int ret = 0;

	pr_debug("%s: msm_hifi_control = %d", __func__,
		 msm_hifi_control);
	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		if (msm_hifi_control == MSM8996_HIFI_ON) {
			if (pdata->hph_en0_gpio < 0) {
				pr_err("%s: hph_en0_gpio is invalid\n",
					__func__);
				ret = -EINVAL;
				goto err;
			}
			gpio_direction_output(pdata->hph_en0_gpio, 1);
		}
		break;
	case SND_SOC_DAPM_PRE_PMD:
		if (msm_hifi_control == MSM8996_HIFI_ON) {
			if (pdata->hph_en0_gpio < 0) {
				pr_err("%s: hph_en0_gpio is invalid\n",
					__func__);
				ret = -EINVAL;
				goto err;
			}
			gpio_direction_output(pdata->hph_en0_gpio, 0);
		}
		break;
	}
err:
	return ret;
}

static int msm_dmic_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *k, int event)
{
	if (SND_SOC_DAPM_EVENT_ON(event))
		usleep_range(10000, 10000);

	return 0;
}

static const struct snd_soc_dapm_widget msm8996_dapm_widgets[] = {

	SND_SOC_DAPM_SUPPLY("MCLK",  SND_SOC_NOPM, 0, 0,
	msm8996_mclk_event, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_SUPPLY("MCLK TX",  SND_SOC_NOPM, 0, 0,
	msm8996_mclk_tx_event, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_SPK("Lineout_1 amp", NULL),
	SND_SOC_DAPM_SPK("Lineout_3 amp", NULL),
	SND_SOC_DAPM_SPK("Lineout_2 amp", NULL),
	SND_SOC_DAPM_SPK("Lineout_4 amp", NULL),
	SND_SOC_DAPM_SPK("ultrasound amp", msm_ext_ultrasound_event),
	SND_SOC_DAPM_SPK("hifi amp", msm_hifi_ctrl_event),
	SND_SOC_DAPM_MIC("Handset Mic", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
#if 0
	SND_SOC_DAPM_MIC("ANCRight Headset Mic", NULL),
	SND_SOC_DAPM_MIC("ANCLeft Headset Mic", NULL),
#else
#ifdef CONFIG_USE_AS_HS
	SND_SOC_DAPM_MIC("ANCRight Headset Mic", msm_headset_lr_event),
	SND_SOC_DAPM_MIC("ANCLeft Headset Mic", msm_headset_lr_event),
#else
	SND_SOC_DAPM_MIC("ANCRight Headset Mic", NULL),
	SND_SOC_DAPM_MIC("ANCLeft Headset Mic", NULL),
#endif
#endif
	SND_SOC_DAPM_MIC("Analog Mic4", NULL),
	SND_SOC_DAPM_MIC("Analog Mic6", NULL),
	SND_SOC_DAPM_MIC("Analog Mic7", NULL),
	SND_SOC_DAPM_MIC("Analog Mic8", NULL),

	SND_SOC_DAPM_MIC("Digital Mic0", NULL),
	SND_SOC_DAPM_MIC("Digital Mic1", NULL),
	SND_SOC_DAPM_MIC("Digital Mic2", NULL),
	SND_SOC_DAPM_MIC("Digital Mic3", NULL),
	SND_SOC_DAPM_MIC("Digital Mic4", NULL),
	SND_SOC_DAPM_MIC("Digital Mic5", NULL),
	SND_SOC_DAPM_MIC("Digital Mic6", NULL),
	SND_SOC_DAPM_MIC("Digital Mic", msm_dmic_event),
};

static struct snd_soc_dapm_route wcd9335_audio_paths[] = {
	{"MIC BIAS1", NULL, "MCLK TX"},
	{"MIC BIAS2", NULL, "MCLK TX"},
	{"MIC BIAS3", NULL, "MCLK TX"},
	{"MIC BIAS4", NULL, "MCLK TX"},
};

static int slim5_rx_sample_rate_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	int sample_rate_val = 0;

	switch (slim5_rx_sample_rate) {
	case SAMPLING_RATE_44P1KHZ:
		sample_rate_val = 3;
		break;

	case SAMPLING_RATE_192KHZ:
		sample_rate_val = 2;
		break;

	case SAMPLING_RATE_96KHZ:
		sample_rate_val = 1;
		break;

	case SAMPLING_RATE_48KHZ:
	default:
		sample_rate_val = 0;
		break;
	}

	ucontrol->value.integer.value[0] = sample_rate_val;
	pr_debug("%s: slim5_rx_sample_rate = %d\n", __func__,
		 slim5_rx_sample_rate);

	return 0;
}

static int slim5_rx_sample_rate_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s: ucontrol value = %ld\n", __func__,
		 ucontrol->value.integer.value[0]);

	switch (ucontrol->value.integer.value[0]) {
	case 3:
		slim5_rx_sample_rate = SAMPLING_RATE_44P1KHZ;
		break;
	case 2:
		slim5_rx_sample_rate = SAMPLING_RATE_192KHZ;
		break;
	case 1:
		slim5_rx_sample_rate = SAMPLING_RATE_96KHZ;
		break;
	case 0:
	default:
		slim5_rx_sample_rate = SAMPLING_RATE_48KHZ;
	}

	pr_debug("%s: slim5_rx_sample_rate = %d\n", __func__,
		 slim5_rx_sample_rate);

	return 0;
}

static int slim6_rx_sample_rate_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	int sample_rate_val = 0;

	switch (slim6_rx_sample_rate) {
	case SAMPLING_RATE_44P1KHZ:
		sample_rate_val = 3;
		break;

	case SAMPLING_RATE_192KHZ:
		sample_rate_val = 2;
		break;

	case SAMPLING_RATE_96KHZ:
		sample_rate_val = 1;
		break;

	case SAMPLING_RATE_48KHZ:
	default:
		sample_rate_val = 0;
		break;
	}

	ucontrol->value.integer.value[0] = sample_rate_val;
	pr_debug("%s: slim6_rx_sample_rate = %d\n", __func__,
		 slim6_rx_sample_rate);

	return 0;
}

static int slim6_rx_sample_rate_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	switch (ucontrol->value.integer.value[0]) {
	case 3:
		slim6_rx_sample_rate = SAMPLING_RATE_44P1KHZ;
		break;
	case 2:
		slim6_rx_sample_rate = SAMPLING_RATE_192KHZ;
		break;
	case 1:
		slim6_rx_sample_rate = SAMPLING_RATE_96KHZ;
		break;
	case 0:
	default:
		slim6_rx_sample_rate = SAMPLING_RATE_48KHZ;
		break;
	}

	pr_debug("%s: ucontrol value = %ld, slim6_rx_sample_rate = %d\n",
		 __func__, ucontrol->value.integer.value[0],
		 slim6_rx_sample_rate);

	return 0;
}

static int slim0_tx_bit_format_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	switch (slim0_tx_bit_format) {
	case SNDRV_PCM_FORMAT_S24_3LE:
		ucontrol->value.integer.value[0] = 2;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		ucontrol->value.integer.value[0] = 1;
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
	default:
		ucontrol->value.integer.value[0] = 0;
		break;
	}

	pr_debug("%s: slim0_tx_bit_format = %d, ucontrol value = %ld\n",
			 __func__, slim0_tx_bit_format,
			ucontrol->value.integer.value[0]);
	return 0;
}

static int slim0_tx_bit_format_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	int rc = 0;

	switch (ucontrol->value.integer.value[0]) {
	case 2:
		slim0_tx_bit_format = SNDRV_PCM_FORMAT_S24_3LE;
		break;
	case 1:
		slim0_tx_bit_format = SNDRV_PCM_FORMAT_S24_LE;
		break;
	case 0:
		slim0_tx_bit_format = SNDRV_PCM_FORMAT_S16_LE;
		break;
	default:
		pr_err("%s: invalid value %ld\n", __func__,
		       ucontrol->value.integer.value[0]);
		rc = -EINVAL;
		break;
	}

	pr_debug("%s: ucontrol value = %ld, slim0_tx_bit_format = %d\n",
		 __func__, ucontrol->value.integer.value[0],
		 slim0_tx_bit_format);

	return rc;
}

static int slim0_rx_sample_rate_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	int sample_rate_val = 0;

	switch (slim0_rx_sample_rate) {
	case SAMPLING_RATE_32KHZ:
		sample_rate_val = 6;
		break;

	case SAMPLING_RATE_16KHZ:
		sample_rate_val = 5;
		break;

	case SAMPLING_RATE_8KHZ:
		sample_rate_val = 4;
		break;

	case SAMPLING_RATE_44P1KHZ:
		sample_rate_val = 3;
		break;

	case SAMPLING_RATE_192KHZ:
		sample_rate_val = 2;
		break;

	case SAMPLING_RATE_96KHZ:
		sample_rate_val = 1;
		break;

	case SAMPLING_RATE_48KHZ:
	default:
		sample_rate_val = 0;
		break;
	}

	ucontrol->value.integer.value[0] = sample_rate_val;
	pr_debug("%s: slim0_rx_sample_rate = %d\n", __func__,
		 slim0_rx_sample_rate);

	return 0;
}

static int slim0_rx_sample_rate_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s: ucontrol value = %ld\n", __func__,
		 ucontrol->value.integer.value[0]);

	switch (ucontrol->value.integer.value[0]) {
	case 6:
		slim0_rx_sample_rate = SAMPLING_RATE_32KHZ;
		break;
	case 5:
		slim0_rx_sample_rate = SAMPLING_RATE_16KHZ;
		break;
	case 4:
		slim0_rx_sample_rate = SAMPLING_RATE_8KHZ;
		break;
	case 3:
		slim0_rx_sample_rate = SAMPLING_RATE_44P1KHZ;
		break;
	case 2:
		slim0_rx_sample_rate = SAMPLING_RATE_192KHZ;
		break;
	case 1:
		slim0_rx_sample_rate = SAMPLING_RATE_96KHZ;
		break;
	case 0:
	default:
		slim0_rx_sample_rate = SAMPLING_RATE_48KHZ;
	}

	pr_debug("%s: slim0_rx_sample_rate = %d\n", __func__,
		 slim0_rx_sample_rate);

	return 0;
}

static int slim0_tx_sample_rate_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	int sample_rate_val = 0;

	switch (slim0_tx_sample_rate) {
	case SAMPLING_RATE_192KHZ:
		sample_rate_val = 2;
		break;
	case SAMPLING_RATE_96KHZ:
		sample_rate_val = 1;
		break;
	case SAMPLING_RATE_48KHZ:
	default:
		sample_rate_val = 0;
		break;
	}

	ucontrol->value.integer.value[0] = sample_rate_val;
	pr_debug("%s: slim0_tx_sample_rate = %d\n", __func__,
				slim0_tx_sample_rate);
	return 0;
}

static int slim0_tx_sample_rate_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	int rc = 0;

	pr_debug("%s: ucontrol value = %ld\n", __func__,
			ucontrol->value.integer.value[0]);

	switch (ucontrol->value.integer.value[0]) {
	case 2:
		slim0_tx_sample_rate = SAMPLING_RATE_192KHZ;
		break;
	case 1:
		slim0_tx_sample_rate = SAMPLING_RATE_96KHZ;
		break;
	case 0:
		slim0_tx_sample_rate = SAMPLING_RATE_48KHZ;
		break;
	default:
		rc = -EINVAL;
		pr_err("%s: invalid sample rate being passed\n", __func__);
		break;
	}

	pr_debug("%s: slim0_tx_sample_rate = %d\n", __func__,
			slim0_tx_sample_rate);
	return rc;
}

static int slim5_rx_bit_format_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{

	switch (slim5_rx_bit_format) {
	case SNDRV_PCM_FORMAT_S24_3LE:
		ucontrol->value.integer.value[0] = 2;
		break;

	case SNDRV_PCM_FORMAT_S24_LE:
		ucontrol->value.integer.value[0] = 1;
		break;

	case SNDRV_PCM_FORMAT_S16_LE:
	default:
		ucontrol->value.integer.value[0] = 0;
		break;
	}

	pr_debug("%s: slim5_rx_bit_format = %d, ucontrol value = %ld\n",
		 __func__, slim5_rx_bit_format,
			ucontrol->value.integer.value[0]);

	return 0;
}

static int slim5_rx_bit_format_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	switch (ucontrol->value.integer.value[0]) {
	case 2:
		slim5_rx_bit_format = SNDRV_PCM_FORMAT_S24_3LE;
		break;
	case 1:
		slim5_rx_bit_format = SNDRV_PCM_FORMAT_S24_LE;
		break;
	case 0:
	default:
		slim5_rx_bit_format = SNDRV_PCM_FORMAT_S16_LE;
		break;
	}
	return 0;
}

static int slim6_rx_bit_format_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{

	switch (slim6_rx_bit_format) {
	case SNDRV_PCM_FORMAT_S24_3LE:
		ucontrol->value.integer.value[0] = 2;
		break;

	case SNDRV_PCM_FORMAT_S24_LE:
		ucontrol->value.integer.value[0] = 1;
		break;

	case SNDRV_PCM_FORMAT_S16_LE:
	default:
		ucontrol->value.integer.value[0] = 0;
		break;
	}

	pr_debug("%s: slim6_rx_bit_format = %d, ucontrol value = %ld\n",
		 __func__, slim6_rx_bit_format,
		 ucontrol->value.integer.value[0]);

	return 0;
}

static int slim6_rx_bit_format_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	switch (ucontrol->value.integer.value[0]) {
	case 2:
		slim6_rx_bit_format = SNDRV_PCM_FORMAT_S24_3LE;
		break;
	case 1:
		slim6_rx_bit_format = SNDRV_PCM_FORMAT_S24_LE;
		break;
	case 0:
	default:
		slim6_rx_bit_format = SNDRV_PCM_FORMAT_S16_LE;
		break;
	}
	return 0;
}

static int slim0_rx_bit_format_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{

	switch (slim0_rx_bit_format) {
	case SNDRV_PCM_FORMAT_S24_3LE:
		ucontrol->value.integer.value[0] = 2;
		break;

	case SNDRV_PCM_FORMAT_S24_LE:
		ucontrol->value.integer.value[0] = 1;
		break;

	case SNDRV_PCM_FORMAT_S16_LE:
	default:
		ucontrol->value.integer.value[0] = 0;
		break;
	}

	pr_debug("%s: slim0_rx_bit_format = %d, ucontrol value = %ld\n",
		 __func__, slim0_rx_bit_format,
			ucontrol->value.integer.value[0]);

	return 0;
}

static int slim0_rx_bit_format_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	switch (ucontrol->value.integer.value[0]) {
	case 2:
		slim0_rx_bit_format = SNDRV_PCM_FORMAT_S24_3LE;
		break;
	case 1:
		slim0_rx_bit_format = SNDRV_PCM_FORMAT_S24_LE;
		break;
	case 0:
	default:
		slim0_rx_bit_format = SNDRV_PCM_FORMAT_S16_LE;
		break;
	}
	return 0;
}

static int msm_slim_5_rx_ch_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s: msm_slim_5_rx_ch  = %d\n", __func__,
		 msm_slim_5_rx_ch);
	ucontrol->value.integer.value[0] = msm_slim_5_rx_ch - 1;
	return 0;
}

static int msm_slim_5_rx_ch_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	msm_slim_5_rx_ch = ucontrol->value.integer.value[0] + 1;
	pr_debug("%s: msm_slim_5_rx_ch = %d\n", __func__,
		 msm_slim_5_rx_ch);
	return 1;
}

static int msm_slim_6_rx_ch_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s: msm_slim_6_rx_ch  = %d\n", __func__,
		 msm_slim_6_rx_ch);
	ucontrol->value.integer.value[0] = msm_slim_6_rx_ch - 1;
	return 0;
}

static int msm_slim_6_rx_ch_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	msm_slim_6_rx_ch = ucontrol->value.integer.value[0] + 1;
	pr_debug("%s: msm_slim_6_rx_ch = %d\n", __func__,
		 msm_slim_6_rx_ch);
	return 1;
}

static int msm_slim_0_rx_ch_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s: msm_slim_0_rx_ch  = %d\n", __func__,
		 msm_slim_0_rx_ch);
	ucontrol->value.integer.value[0] = msm_slim_0_rx_ch - 1;
	return 0;
}

static int msm_slim_0_rx_ch_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	msm_slim_0_rx_ch = ucontrol->value.integer.value[0] + 1;
	pr_debug("%s: msm_slim_0_rx_ch = %d\n", __func__,
		 msm_slim_0_rx_ch);
	return 1;
}

static int msm_slim_0_tx_ch_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s: msm_slim_0_tx_ch  = %d\n", __func__,
		 msm_slim_0_tx_ch);
	ucontrol->value.integer.value[0] = msm_slim_0_tx_ch - 1;
	return 0;
}

static int msm_slim_0_tx_ch_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	msm_slim_0_tx_ch = ucontrol->value.integer.value[0] + 1;
	pr_debug("%s: msm_slim_0_tx_ch = %d\n", __func__, msm_slim_0_tx_ch);
	return 1;
}

static int msm_pri_mi2s_rx_ch_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s: msm_pri_mi2s_rx_ch  = %d\n", __func__,
		 msm_pri_mi2s_rx_ch);
	ucontrol->value.integer.value[0] = msm_pri_mi2s_rx_ch - 1;
	return 0;
}

static int msm_pri_mi2s_rx_ch_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	msm_pri_mi2s_rx_ch = ucontrol->value.integer.value[0] + 1;
	pr_debug("%s: msm_pri_mi2s_rx_ch = %d\n", __func__,
		 msm_pri_mi2s_rx_ch);
	return 1;

}

static int msm_sec_mi2s_rx_ch_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s: msm_sec_mi2s_rx_ch  = %d\n", __func__,
		 msm_sec_mi2s_rx_ch);
	ucontrol->value.integer.value[0] = msm_sec_mi2s_rx_ch - 1;
	return 0;
}

static int msm_sec_mi2s_rx_ch_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	msm_sec_mi2s_rx_ch = ucontrol->value.integer.value[0] + 1;
	pr_debug("%s: msm_sec_mi2s_rx_ch = %d\n", __func__,
		 msm_sec_mi2s_rx_ch);
	return 1;
}

static int msm_tert_mi2s_rx_ch_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s: msm_tert_mi2s_rx_ch  = %d\n", __func__,
		 msm_tert_mi2s_rx_ch);
	ucontrol->value.integer.value[0] = msm_tert_mi2s_rx_ch - 1;
	return 0;
}

static int msm_tert_mi2s_rx_ch_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	msm_tert_mi2s_rx_ch = ucontrol->value.integer.value[0] + 1;
	pr_debug("%s: msm_tert_mi2s_rx_ch = %d\n", __func__,
		 msm_tert_mi2s_rx_ch);
	return 1;
}

static int msm_quat_mi2s_rx_ch_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s: msm_quat_mi2s_rx_ch  = %d\n", __func__,
		 msm_quat_mi2s_rx_ch);
	ucontrol->value.integer.value[0] = msm_quat_mi2s_rx_ch - 1;
	return 0;
}

static int msm_quat_mi2s_rx_ch_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	msm_quat_mi2s_rx_ch = ucontrol->value.integer.value[0] + 1;
	pr_debug("%s: msm_quat_mi2s_rx_ch = %d\n", __func__,
		 msm_quat_mi2s_rx_ch);
	return 1;
}

static int msm_pri_mi2s_tx_ch_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s: msm_pri_mi2s_tx_ch  = %d\n", __func__,
		 msm_pri_mi2s_tx_ch);
	ucontrol->value.integer.value[0] = msm_pri_mi2s_tx_ch - 1;
	return 0;
}

static int msm_pri_mi2s_tx_ch_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	msm_pri_mi2s_tx_ch = ucontrol->value.integer.value[0] + 1;
	pr_debug("%s: msm_pri_mi2s_tx_ch = %d\n", __func__,
		 msm_pri_mi2s_tx_ch);
	return 1;

}

static int msm_sec_mi2s_tx_ch_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s: msm_sec_mi2s_tx_ch  = %d\n", __func__,
		 msm_sec_mi2s_tx_ch);
	ucontrol->value.integer.value[0] = msm_sec_mi2s_tx_ch - 1;
	return 0;
}

static int msm_sec_mi2s_tx_ch_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	msm_sec_mi2s_tx_ch = ucontrol->value.integer.value[0] + 1;
	pr_debug("%s: msm_sec_mi2s_tx_ch = %d\n", __func__,
		 msm_sec_mi2s_tx_ch);
	return 1;
}

static int msm_tert_mi2s_tx_ch_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s: msm_tert_mi2s_tx_ch  = %d\n", __func__,
		 msm_tert_mi2s_tx_ch);
	ucontrol->value.integer.value[0] = msm_tert_mi2s_tx_ch - 1;
	return 0;
}

static int msm_tert_mi2s_tx_ch_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	msm_tert_mi2s_tx_ch = ucontrol->value.integer.value[0] + 1;
	pr_debug("%s: msm_tert_mi2s_tx_ch = %d\n", __func__,
		 msm_tert_mi2s_tx_ch);
	return 1;
}

static int msm_quat_mi2s_tx_ch_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s: msm_quat_mi2s_tx_ch  = %d\n", __func__,
		 msm_quat_mi2s_tx_ch);
	ucontrol->value.integer.value[0] = msm_quat_mi2s_tx_ch - 1;
	return 0;
}

static int msm_quat_mi2s_tx_ch_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	msm_quat_mi2s_tx_ch = ucontrol->value.integer.value[0] + 1;
	pr_debug("%s: msm_quat_mi2s_tx_ch = %d\n", __func__,
		 msm_quat_mi2s_tx_ch);
	return 1;
}

static int msm_slim_1_tx_ch_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s: msm_slim_1_tx_ch  = %d\n", __func__,
		 msm_slim_1_tx_ch);
	ucontrol->value.integer.value[0] = msm_slim_1_tx_ch - 1;
	return 0;
}

static int msm_slim_1_tx_ch_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	msm_slim_1_tx_ch = ucontrol->value.integer.value[0] + 1;

	pr_debug("%s: msm_slim_1_tx_ch = %d\n", __func__, msm_slim_1_tx_ch);
	return 1;
}

static int msm_vi_feed_tx_ch_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = msm_vi_feed_tx_ch - 1;
	pr_debug("%s: msm_vi_feed_tx_ch = %ld\n", __func__,
		 ucontrol->value.integer.value[0]);
	return 0;
}

static int msm_vi_feed_tx_ch_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	msm_vi_feed_tx_ch = ucontrol->value.integer.value[0] + 1;

	pr_debug("%s: msm_vi_feed_tx_ch = %d\n", __func__, msm_vi_feed_tx_ch);
	return 1;
}

static int hdmi_rx_bit_format_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{

	switch (hdmi_rx_bit_format) {
	case SNDRV_PCM_FORMAT_S24_3LE:
		ucontrol->value.integer.value[0] = 2;
		break;

	case SNDRV_PCM_FORMAT_S24_LE:
		ucontrol->value.integer.value[0] = 1;
		break;

	case SNDRV_PCM_FORMAT_S16_LE:
	default:
		ucontrol->value.integer.value[0] = 0;
		break;
	}

	pr_debug("%s: hdmi_rx_bit_format = %d, ucontrol value = %ld\n",
		 __func__, hdmi_rx_bit_format,
			ucontrol->value.integer.value[0]);

	return 0;
}

static int hdmi_rx_bit_format_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	switch (ucontrol->value.integer.value[0]) {
	case 2:
		hdmi_rx_bit_format = SNDRV_PCM_FORMAT_S24_3LE;
		break;
	case 1:
		hdmi_rx_bit_format = SNDRV_PCM_FORMAT_S24_LE;
		break;
	case 0:
	default:
		hdmi_rx_bit_format = SNDRV_PCM_FORMAT_S16_LE;
		break;
	}
	pr_debug("%s: hdmi_rx_bit_format = %d, ucontrol value = %ld\n",
		 __func__, hdmi_rx_bit_format,
			ucontrol->value.integer.value[0]);
	return 0;
}

static int msm_hdmi_rx_ch_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s: msm_hdmi_rx_ch  = %d\n", __func__,
		 msm_hdmi_rx_ch);
	ucontrol->value.integer.value[0] = msm_hdmi_rx_ch - 2;

	return 0;
}

static int msm_hdmi_rx_ch_put(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	msm_hdmi_rx_ch = ucontrol->value.integer.value[0] + 2;
	if (msm_hdmi_rx_ch > 8) {
		pr_err("%s: channels %d exceeded 8.Limiting to max chs-8\n",
			__func__, msm_hdmi_rx_ch);
		msm_hdmi_rx_ch = 8;
	}
	pr_debug("%s: msm_hdmi_rx_ch = %d\n", __func__, msm_hdmi_rx_ch);

	return 1;
}

static int hdmi_rx_sample_rate_get(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	int sample_rate_val = 0;

	switch (hdmi_rx_sample_rate) {
	case SAMPLING_RATE_192KHZ:
		sample_rate_val = 2;
		break;

	case SAMPLING_RATE_96KHZ:
		sample_rate_val = 1;
		break;

	case SAMPLING_RATE_48KHZ:
	default:
		sample_rate_val = 0;
		break;
	}

	ucontrol->value.integer.value[0] = sample_rate_val;
	pr_debug("%s: hdmi_rx_sample_rate = %d\n", __func__,
		 hdmi_rx_sample_rate);

	return 0;
}

static int hdmi_rx_sample_rate_put(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s: ucontrol value = %ld\n", __func__,
		 ucontrol->value.integer.value[0]);

	switch (ucontrol->value.integer.value[0]) {
	case 2:
		hdmi_rx_sample_rate = SAMPLING_RATE_192KHZ;
		break;
	case 1:
		hdmi_rx_sample_rate = SAMPLING_RATE_96KHZ;
		break;
	case 0:
	default:
		hdmi_rx_sample_rate = SAMPLING_RATE_48KHZ;
	}

	pr_debug("%s: hdmi_rx_sample_rate = %d\n", __func__,
		 hdmi_rx_sample_rate);

	return 0;
}

static int msm8996_auxpcm_rate_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = msm8996_auxpcm_rate;
	return 0;
}

static int msm8996_auxpcm_rate_put(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	switch (ucontrol->value.integer.value[0]) {
	case 0:
		msm8996_auxpcm_rate = SAMPLING_RATE_8KHZ;
		break;
	case 1:
		msm8996_auxpcm_rate = SAMPLING_RATE_16KHZ;
		break;
	default:
		msm8996_auxpcm_rate = SAMPLING_RATE_8KHZ;
		break;
	}
	return 0;
}

static int mi2_get_sample_rate(int sample_rate)
{
	int sample_rate_val = 0;

	switch (sample_rate) {
		case SAMPLING_RATE_192KHZ:
			sample_rate_val = 5;
			break;

		case SAMPLING_RATE_96KHZ:
			sample_rate_val = 4;
			break;

		case SAMPLING_RATE_48KHZ:
			sample_rate_val = 3;
			break;

		case SAMPLING_RATE_32KHZ:
			sample_rate_val = 2;
			break;

		case SAMPLING_RATE_16KHZ:
			sample_rate_val = 1;
			break;

		case SAMPLING_RATE_8KHZ:
		default:
			sample_rate_val = 0;
			break;
	}

	return sample_rate_val;
}

static int pri_mi2s_sample_rate_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = mi2_get_sample_rate(pri_mi2s_sample_rate);
	pr_debug("%s: pri_mi2s_sample_rate = %d\n", __func__,
				pri_mi2s_sample_rate);

	return 0;
}

static int sec_mi2s_sample_rate_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = mi2_get_sample_rate(sec_mi2s_sample_rate);
	pr_debug("%s: sec_mi2s_sample_rate = %d\n", __func__,
				sec_mi2s_sample_rate);

	return 0;
}

static int tert_mi2s_sample_rate_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = mi2_get_sample_rate(tert_mi2s_sample_rate);
	pr_debug("%s: tert_mi2s_sample_rate = %d\n", __func__,
				tert_mi2s_sample_rate);

	return 0;
}

static int quat_mi2s_sample_rate_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = mi2_get_sample_rate(quat_mi2s_sample_rate);
	pr_debug("%s: quat_mi2s_sample_rate = %d\n", __func__,
				quat_mi2s_sample_rate);

	return 0;
}

static int mi2s_set_sample_rate(int sample_rate_idx)
{
	int sample_rate;

	switch (sample_rate_idx) {
	case 0:
		sample_rate = SAMPLING_RATE_8KHZ;
		break;
	case 1:
		sample_rate = SAMPLING_RATE_16KHZ;
		break;
	case 2:
		sample_rate = SAMPLING_RATE_32KHZ;
		break;
	case 3:
		sample_rate = SAMPLING_RATE_48KHZ;
		break;
	case 4:
		sample_rate = SAMPLING_RATE_96KHZ;
		break;
	case 5:
		sample_rate = SAMPLING_RATE_192KHZ;
		break;
	default:
		pr_warn("%s: sample_rate = %d not supported, using default\n",
			__func__, sample_rate_idx);
		sample_rate = SAMPLING_RATE_48KHZ;
	}

	return sample_rate;
}

static int pri_mi2s_sample_rate_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	pri_mi2s_sample_rate = mi2s_set_sample_rate(
					     ucontrol->value.integer.value[0]);
	pr_debug("%s: sample_rate = %d\n", __func__, pri_mi2s_sample_rate);

	return 0;
}

static int sec_mi2s_sample_rate_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	sec_mi2s_sample_rate = mi2s_set_sample_rate(
					     ucontrol->value.integer.value[0]);
	pr_debug("%s: sample_rate = %d\n", __func__, sec_mi2s_sample_rate);

	return 0;
}

static int tert_mi2s_sample_rate_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	tert_mi2s_sample_rate = mi2s_set_sample_rate(
					     ucontrol->value.integer.value[0]);
	pr_debug("%s: sample_rate = %d\n", __func__, tert_mi2s_sample_rate);

	return 0;
}

static int quat_mi2s_sample_rate_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	quat_mi2s_sample_rate = mi2s_set_sample_rate(
					     ucontrol->value.integer.value[0]);
	pr_debug("%s: sample_rate = %d\n", __func__, quat_mi2s_sample_rate);

	return 0;
}

static int mi2s_get_bit_format(int bit_format_idx)
{
	int bit_format;

	switch (bit_format_idx) {
	case SNDRV_PCM_FORMAT_S24_LE:
		bit_format = 1;
		break;

	case SNDRV_PCM_FORMAT_S16_LE:
	default:
		bit_format = 0;
		break;
	}

	return bit_format;
}

static int pri_mi2s_bit_format_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = mi2s_get_bit_format(
							pri_mi2s_bit_format);

	pr_debug("%s: pri_mi2s_bit_format = %d, ucontrol value = %ld\n",
		 __func__, pri_mi2s_bit_format,
			ucontrol->value.integer.value[0]);

	return 0;
}

static int sec_mi2s_bit_format_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = mi2s_get_bit_format(
							sec_mi2s_bit_format);

	pr_debug("%s: sec_mi2s_bit_format = %d, ucontrol value = %ld\n",
		 __func__, sec_mi2s_bit_format,
			ucontrol->value.integer.value[0]);

	return 0;
}

static int tert_mi2s_bit_format_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = mi2s_get_bit_format(
							tert_mi2s_bit_format);

	pr_debug("%s: tert_mi2s_bit_format = %d, ucontrol value = %ld\n",
		 __func__, tert_mi2s_bit_format,
			ucontrol->value.integer.value[0]);

	return 0;
}

static int quat_mi2s_bit_format_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = mi2s_get_bit_format(
							quat_mi2s_bit_format);

	pr_debug("%s: quat_mi2s_bit_format = %d, ucontrol value = %ld\n",
		 __func__, quat_mi2s_bit_format,
			ucontrol->value.integer.value[0]);

	return 0;
}

static int mi2s_put_bit_format(int bit_format_idx)
{
	int bit_format;

	switch (bit_format_idx) {
	case 0:
		bit_format = SNDRV_PCM_FORMAT_S16_LE;
		break;
	case 1:
		bit_format = SNDRV_PCM_FORMAT_S24_LE;
		break;
	default:
		pr_warn("%s: bit_format = %d not supported, using default\n",
			__func__, bit_format_idx);
		bit_format = SNDRV_PCM_FORMAT_S16_LE;
		break;
	}

	return bit_format;
}

static int pri_mi2s_bit_format_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	pri_mi2s_bit_format = mi2s_put_bit_format(ucontrol->value.integer.value[0]);
	pr_debug("%s: bit_format = %d \n", __func__, pri_mi2s_bit_format);

	return 0;
}

static int sec_mi2s_bit_format_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	sec_mi2s_bit_format = mi2s_put_bit_format(ucontrol->value.integer.value[0]);
	pr_debug("%s: bit_format = %d \n", __func__, sec_mi2s_bit_format);

	return 0;
}

static int tert_mi2s_bit_format_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	tert_mi2s_bit_format = mi2s_put_bit_format(ucontrol->value.integer.value[0]);
	pr_debug("%s: bit_format = %d \n", __func__, tert_mi2s_bit_format);

	return 0;
}

static int quat_mi2s_bit_format_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	quat_mi2s_bit_format = mi2s_put_bit_format(ucontrol->value.integer.value[0]);
	pr_debug("%s: bit_format = %d \n", __func__, quat_mi2s_bit_format);

	return 0;
}

static int msm_proxy_rx_ch_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	pr_debug("%s: msm_proxy_rx_ch = %d\n", __func__, msm_proxy_rx_ch);
	ucontrol->value.integer.value[0] = msm_proxy_rx_ch - 1;
	return 0;
}

static int msm_proxy_rx_ch_put(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	msm_proxy_rx_ch = ucontrol->value.integer.value[0] + 1;
	pr_debug("%s: msm_proxy_rx_ch = %d\n", __func__, msm_proxy_rx_ch);
	return 1;
}

static int msm_auxpcm_be_params_fixup(struct snd_soc_pcm_runtime *rtd,
				      struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate =
	    hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);

	struct snd_interval *channels =
	    hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);

	rate->min = rate->max = msm8996_auxpcm_rate;
	channels->min = channels->max = 1;

	return 0;
}

static int msm_proxy_rx_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
					   struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_CHANNELS);

	pr_debug("%s: msm_proxy_rx_ch =%d\n", __func__, msm_proxy_rx_ch);

	if (channels->max < 2)
		channels->min = channels->max = 2;
	channels->min = channels->max = msm_proxy_rx_ch;
	rate->min = rate->max = SAMPLING_RATE_48KHZ;
	return 0;
}

static int msm_proxy_tx_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
					   struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);

	rate->min = rate->max = SAMPLING_RATE_48KHZ;
	return 0;
}

static int msm8996_hdmi_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
					      struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_CHANNELS);

	pr_debug("%s channels->min %u channels->max %u ()\n", __func__,
		 channels->min, channels->max);

	param_set_mask(params, SNDRV_PCM_HW_PARAM_FORMAT,
				hdmi_rx_bit_format);
	if (channels->max < 2)
		channels->min = channels->max = 2;
	rate->min = rate->max = hdmi_rx_sample_rate;
	channels->min = channels->max = msm_hdmi_rx_ch;

	return 0;
}

static int msm_tx_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
				     struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_CHANNELS);

	pr_debug("%s: channel:%d\n", __func__, msm_tert_mi2s_tx_ch);
	rate->min = rate->max = SAMPLING_RATE_48KHZ;
	channels->min = channels->max = msm_tert_mi2s_tx_ch;
	return 0;
}

static int legacy_msm8996_mi2s_snd_startup(struct snd_pcm_substream *substream)
{
	int ret = 0;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;

	pr_debug("%s: substream = %s  stream = %d\n", __func__,
		 substream->name, substream->stream);

	mi2s_tx_clk.enable = 1;
	ret = afe_set_lpass_clock_v2(AFE_PORT_ID_TERTIARY_MI2S_TX,
				&mi2s_tx_clk);
	if (ret < 0) {
		pr_err("%s: afe lpass clock failed, err:%d\n", __func__, ret);
		goto err;
	}
	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0)
		pr_err("%s: set fmt cpu dai failed, err:%d\n", __func__, ret);
err:
	return ret;
}

static void legacy_msm8996_mi2s_snd_shutdown(struct snd_pcm_substream *substream)
{
	int ret = 0;

	pr_debug("%s: substream = %s  stream = %d\n", __func__,
		substream->name, substream->stream);

	mi2s_tx_clk.enable = 0;
	ret = afe_set_lpass_clock_v2(AFE_PORT_ID_TERTIARY_MI2S_TX,
				&mi2s_tx_clk);
	if (ret < 0)
		pr_err("%s: afe lpass clock failed, err:%d\n", __func__, ret);
}

static int msm_htc_ftm_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct aud_btpcm_config *pconfig = &htc_aud_btpcm_config;
	int value = ucontrol->value.integer.value[0];
	int i = 0;

	if (htc_aud_btpcm_config.init == 0) {
		pr_err("%s: init ftm btpcm failed, skip control\n", __func__);
		return -EPERM;
	}

	switch (value) {
	case 1:
		set_pinctrl_ftm_mode(1);
		for(i = 0; i < ARRAY_SIZE(pconfig->gpio); i++) {
			if (strncmp(pconfig->gpio[i].gpio_name, "ftm-btpcm-din", sizeof("ftm-btpcm-din")))
				gpio_direction_input(pconfig->gpio[i].gpio_no);
		}
		pconfig->ftm = 1;
		break;

	case 0:
	default:
		for(i = 0; i < ARRAY_SIZE(pconfig->gpio); i++) {
			if (strncmp(pconfig->gpio[i].gpio_name, "ftm-btpcm-din", sizeof("ftm-btpcm-din")))
				gpio_direction_output(pconfig->gpio[i].gpio_no, 1);
		}
		set_pinctrl_ftm_mode(0);
		pconfig->ftm = 0;
	}

	pr_debug("%s: set ftm mode = %d\n",__func__, value);
	return 0;
}

static int msm_htc_ftm_get(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct aud_btpcm_config *pconfig = &htc_aud_btpcm_config;
	int i = 0;
	int state = 0;

	if (htc_aud_btpcm_config.init == 0) {
		pr_err("%s: init ftm btpcm failed, skip control\n", __func__);
		return -EPERM;
	}

	ucontrol->value.integer.value[0] = -1;
	if(!pconfig->ftm)
		pr_info("%s: non ftm state\n",__func__);

	for(i = 0; i < ARRAY_SIZE(pconfig->gpio); i++) {
		state += gpio_get_value(pconfig->gpio[i].gpio_no);
		pr_info("ftm gpio value %d on No.%d\n",gpio_get_value(pconfig->gpio[i].gpio_no), i);
		state <<= 1;
	}
	state >>= 1;

	ucontrol->value.integer.value[0] = state;
	pr_debug("%s: get ftm BT gpio high = %ld\n",
			 __func__, ucontrol->value.integer.value[0]);
	return 0;
}

static int msm_htc_as20_vol_index_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	int value = ucontrol->value.integer.value[0];

	if (value < 0 || value > ARRAY_SIZE(htc_as20_vol_index) -1)
		htc_as20_volume_index = 0;
	else
		htc_as20_volume_index = value;

	pr_info("%s, value %d, htc_as20_volume_index %d\n", __func__ , value, htc_as20_volume_index);

	return 0;
}

static int msm_htc_as20_vol_index_get(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	pr_info("%s, htc_as20_volume_index %d\n", __func__ , htc_as20_volume_index);
	ucontrol->value.integer.value[0] = htc_as20_volume_index;

	return 0;
}

static struct snd_soc_ops legacy_msm8996_mi2s_be_ops = {
	.startup = legacy_msm8996_mi2s_snd_startup,
	.shutdown = legacy_msm8996_mi2s_snd_shutdown,
};

static int msm_slim_5_rx_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
					    struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_CHANNELS);

	param_set_mask(params, SNDRV_PCM_HW_PARAM_FORMAT,
				   slim5_rx_bit_format);
	rate->min = rate->max = slim5_rx_sample_rate;
	channels->min = channels->max = msm_slim_5_rx_ch;

	 pr_debug("%s: format = %d, rate = %d, channels = %d\n",
		  __func__, params_format(params), params_rate(params),
		  msm_slim_5_rx_ch);

	return 0;
}

static int msm_slim_6_rx_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
					    struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_CHANNELS);

	param_set_mask(params, SNDRV_PCM_HW_PARAM_FORMAT,
				   slim6_rx_bit_format);
	rate->min = rate->max = slim6_rx_sample_rate;
	channels->min = channels->max = msm_slim_6_rx_ch;

	pr_debug("%s: format = %d, rate = %d, channels = %d\n",
		 __func__, params_format(params), params_rate(params),
		 msm_slim_6_rx_ch);

	return 0;
}

static int msm_slim_0_rx_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
					    struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_CHANNELS);

	param_set_mask(params, SNDRV_PCM_HW_PARAM_FORMAT,
				   slim0_rx_bit_format);
	rate->min = rate->max = slim0_rx_sample_rate;
	channels->min = channels->max = msm_slim_0_rx_ch;

	 pr_debug("%s: format = %d, rate = %d, channels = %d\n",
		  __func__, params_format(params), params_rate(params),
		  msm_slim_0_rx_ch);

	return 0;
}

static int msm_slim_0_tx_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
					    struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_CHANNELS);

	pr_debug("%s()\n", __func__);
	param_set_mask(params, SNDRV_PCM_HW_PARAM_FORMAT, slim0_tx_bit_format);
	rate->min = rate->max = slim0_tx_sample_rate;
	channels->min = channels->max = msm_slim_0_tx_ch;

	return 0;
}

static int msm_slim_1_tx_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
					    struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_CHANNELS);

	pr_debug("%s()\n", __func__);
	param_set_mask(params, SNDRV_PCM_HW_PARAM_FORMAT, slim1_tx_bit_format);
	rate->min = rate->max = slim1_tx_sample_rate;
	channels->min = channels->max = msm_slim_1_tx_ch;

	return 0;
}

static int msm_slim_4_tx_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
					    struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);

	struct snd_interval *channels = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_CHANNELS);

	param_set_mask(params, SNDRV_PCM_HW_PARAM_FORMAT,
		       SNDRV_PCM_FORMAT_S32_LE);

	rate->min = rate->max = SAMPLING_RATE_8KHZ;
	channels->min = channels->max = msm_vi_feed_tx_ch;
	pr_debug("%s: msm_vi_feed_tx_ch: %d\n", __func__, msm_vi_feed_tx_ch);

	return 0;
}

static int msm_slim_5_tx_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
					    struct snd_pcm_hw_params *params)
{
	int rc = 0;
	void *config = NULL;
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_CHANNELS);

	pr_debug("%s: enter\n", __func__);
	rate->min = rate->max = SAMPLING_RATE_16KHZ;
	channels->min = channels->max = 1;

	config = msm8996_codec_fn.get_afe_config_fn(codec,
				AFE_SLIMBUS_SLAVE_PORT_CONFIG);
	if (config) {
		rc = afe_set_config(AFE_SLIMBUS_SLAVE_PORT_CONFIG, config,
				    SLIMBUS_5_TX);
		if (rc) {
			pr_err("%s: Failed to set slimbus slave port config %d\n",
				__func__, rc);
		}
	}

	return rc;
}

static int msm_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
				  struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);

	pr_debug("%s:\n", __func__);
	rate->min = rate->max = SAMPLING_RATE_48KHZ;
	return 0;
}

static u32 msm8996_get_mi2s_bit_clock(int mi2s_bit_format, int sample_rate)
{
	u32 bit_clock = 0;

	if (mi2s_bit_format == SNDRV_PCM_FORMAT_S24_LE) {
		switch (sample_rate) {
			case SAMPLING_RATE_192KHZ:
				bit_clock = Q6AFE_LPASS_IBIT_CLK_12_P288_MHZ;
				break;
			case SAMPLING_RATE_96KHZ:
				bit_clock = Q6AFE_LPASS_IBIT_CLK_6_P144_MHZ;
				break;
			case SAMPLING_RATE_48KHZ:
				bit_clock = Q6AFE_LPASS_IBIT_CLK_3_P072_MHZ;
				break;
			case SAMPLING_RATE_32KHZ:
				bit_clock = Q6AFE_LPASS_IBIT_CLK_2_P048_MHZ;
				break;
			case SAMPLING_RATE_16KHZ:
				bit_clock = Q6AFE_LPASS_IBIT_CLK_1_P024_MHZ;
				break;
			case SAMPLING_RATE_8KHZ:
				bit_clock = Q6AFE_LPASS_IBIT_CLK_512_KHZ;
				break;
			default:
				bit_clock = Q6AFE_LPASS_IBIT_CLK_3_P072_MHZ;
		}
	} else {
		
		switch(sample_rate) {
			case SAMPLING_RATE_192KHZ:
				bit_clock = Q6AFE_LPASS_IBIT_CLK_6_P144_MHZ;
				break;
			case SAMPLING_RATE_96KHZ:
				bit_clock = Q6AFE_LPASS_IBIT_CLK_3_P072_MHZ;
				break;
			case SAMPLING_RATE_48KHZ:
				bit_clock = Q6AFE_LPASS_IBIT_CLK_1_P536_MHZ;
				break;
			case SAMPLING_RATE_32KHZ:
				bit_clock = Q6AFE_LPASS_IBIT_CLK_1_P024_MHZ;
				break;
			case SAMPLING_RATE_16KHZ:
				bit_clock = Q6AFE_LPASS_IBIT_CLK_512_KHZ;
				break;
			case SAMPLING_RATE_8KHZ:
				bit_clock = Q6AFE_LPASS_IBIT_CLK_256_KHZ;
				break;
			default:
				bit_clock = Q6AFE_LPASS_IBIT_CLK_1_P536_MHZ;
				break;
		}
	}
	pr_debug("%s: bit_width = %d, sample_rate = %d, bit_clock = %d\n",
			__func__, mi2s_bit_format, sample_rate, bit_clock);

	return bit_clock;
}

static int msm8996_mi2s_snd_startup(struct snd_pcm_substream *substream,
			int port_id, struct msm_mi2s_data *msm_mi2s_data)
{
	int ret = 0;
	u32 bit_clk = 0;
	u32 dai_format = 0;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct msm_mi2s_pdata *mi2s_pdata =
			(struct msm_mi2s_pdata *) cpu_dai->dev->platform_data;
	u32 ext_mclk_rate = (u32) mi2s_pdata->ext_mclk_rate;

	pr_debug("%s: dai name %s %p substream = %s  stream = %d port_id = %d slave %d ext_mclk_rate %u\n",
		 __func__, cpu_dai->name, cpu_dai->dev, substream->name,
		 substream->stream, port_id, mi2s_pdata->slave, ext_mclk_rate);

	if (atomic_inc_return(&msm_mi2s_data->mi2s_rsc_ref) == 1) {
		bit_clk = msm8996_get_mi2s_bit_clock(*(msm_mi2s_data->bit_format),
						*(msm_mi2s_data->sample_rate));
		msm_mi2s_data->mi2s_clk.enable = 1;
		msm_mi2s_data->mi2s_clk.clk_freq_in_hz = bit_clk;

		if (!mi2s_pdata->slave) {
			dai_format = SND_SOC_DAIFMT_CBS_CFS;

			if (ext_mclk_rate) {
				
				msm_mi2s_data->mi2s_mclk.enable = 1;
				msm_mi2s_data->mi2s_mclk.clk_freq_in_hz = ext_mclk_rate;

				pr_debug("%s: Enabling mclk, clk_freq_in_hz = %u\n",
					__func__, msm_mi2s_data->mi2s_mclk.clk_freq_in_hz); 

				ret = afe_set_lpass_clock_v2(port_id,
						    &msm_mi2s_data->mi2s_mclk);
				if (ret < 0) {
					pr_err("%s: afe lpass mclk failed, err:%d\n",
						__func__, ret);
					atomic_dec_return(&msm_mi2s_data->mi2s_rsc_ref); 
					goto err;
				}
			}
		} else {
			msm_mi2s_data->mi2s_clk.clk_id += 1;
			dai_format = SND_SOC_DAIFMT_CBM_CFM;
		}

		pr_debug("%s: Enabling bit-clock, clk_freq_in_hz = %u\n",
				__func__, msm_mi2s_data->mi2s_clk.clk_freq_in_hz);

		ret = afe_set_lpass_clock_v2(port_id, &msm_mi2s_data->mi2s_clk);
		if (ret < 0) {
			pr_err("%s: afe lpass clock failed, err:%d\n", __func__, ret);
			atomic_dec_return(&msm_mi2s_data->mi2s_rsc_ref);
			goto err;
		}

		ret = snd_soc_dai_set_fmt(cpu_dai, dai_format);
		if (ret < 0)
			pr_err("%s: set fmt cpu dai failed, err:%d\n", __func__, ret);

	}
err:
	return ret;
}

static int msm8996_pri_mi2s_snd_startup(struct snd_pcm_substream *substream)
{
	return msm8996_mi2s_snd_startup(substream, AFE_PORT_ID_PRIMARY_MI2S_RX,
					&msm_pri_mi2s_data);
}

static int msm8996_sec_mi2s_snd_startup(struct snd_pcm_substream *substream)
{
	return msm8996_mi2s_snd_startup(substream, AFE_PORT_ID_SECONDARY_MI2S_RX,
					&msm_sec_mi2s_data);
}

static int msm8996_tert_mi2s_snd_startup(struct snd_pcm_substream *substream)
{
	return msm8996_mi2s_snd_startup(substream, AFE_PORT_ID_TERTIARY_MI2S_RX,
					&msm_tert_mi2s_data);
}

static int msm8996_quat_mi2s_snd_startup(struct snd_pcm_substream *substream)
{
	return msm8996_mi2s_snd_startup(substream, AFE_PORT_ID_QUATERNARY_MI2S_RX,
					&msm_quat_mi2s_data);
}

static void msm8996_mi2s_snd_shutdown(struct snd_pcm_substream *substream,
			int port_id, struct msm_mi2s_data *msm_mi2s_data)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct msm_mi2s_pdata *mi2s_pdata =
			(struct msm_mi2s_pdata *) cpu_dai->dev->platform_data;
	int ret = 0;

	pr_debug("%s: dai name %s %p  substream = %s  stream = %d port_id = %d\n",
		 __func__, cpu_dai->name, cpu_dai->dev,substream->name,
		 substream->stream, port_id);

	if (atomic_dec_return(&msm_mi2s_data->mi2s_rsc_ref) == 0) {
		msm_mi2s_data->mi2s_clk.enable = 0;
		pr_debug("%s: Disabling bit-clk\n", __func__);
		ret = afe_set_lpass_clock_v2(port_id, &msm_mi2s_data->mi2s_clk);
		if (ret < 0)
			pr_err("%s: afe lpass clock failed, err:%d\n",
				__func__, ret);

		
		if (mi2s_pdata->slave)
			msm_mi2s_data->mi2s_clk.clk_id -= 1;

		if (!mi2s_pdata->slave && mi2s_pdata->ext_mclk_rate) {
			msm_mi2s_data->mi2s_mclk.enable = 0;
			pr_debug("%s: Disabling mclk\n", __func__);
			ret = afe_set_lpass_clock_v2(port_id,
						  &msm_mi2s_data->mi2s_mclk);
			if (ret < 0)
				pr_err("%s: afe lpass clock failed, err:%d\n",
					__func__, ret);
		}
	}
}

static void msm8996_pri_mi2s_snd_shutdown(struct snd_pcm_substream *substream)
{
	msm8996_mi2s_snd_shutdown(substream, AFE_PORT_ID_PRIMARY_MI2S_RX,
					&msm_pri_mi2s_data);
}

static void msm8996_sec_mi2s_snd_shutdown(struct snd_pcm_substream *substream)
{
	msm8996_mi2s_snd_shutdown(substream, AFE_PORT_ID_SECONDARY_MI2S_RX,
					&msm_sec_mi2s_data);
}

static void msm8996_tert_mi2s_snd_shutdown(struct snd_pcm_substream *substream)
{
	msm8996_mi2s_snd_shutdown(substream, AFE_PORT_ID_TERTIARY_MI2S_RX,
					&msm_tert_mi2s_data);
}

static void msm8996_quat_mi2s_snd_shutdown(struct snd_pcm_substream *substream)
{
	msm8996_mi2s_snd_shutdown(substream, AFE_PORT_ID_QUATERNARY_MI2S_RX,
					&msm_quat_mi2s_data);
}

static struct snd_soc_ops msm8996_pri_mi2s_be_ops = {
	.startup = msm8996_pri_mi2s_snd_startup,
	.shutdown = msm8996_pri_mi2s_snd_shutdown,
};
static struct snd_soc_ops msm8996_sec_mi2s_be_ops = {
	.startup = msm8996_sec_mi2s_snd_startup,
	.shutdown = msm8996_sec_mi2s_snd_shutdown,
};
static struct snd_soc_ops msm8996_tert_mi2s_be_ops = {
	.startup = msm8996_tert_mi2s_snd_startup,
	.shutdown = msm8996_tert_mi2s_snd_shutdown,
};
static struct snd_soc_ops msm8996_quat_mi2s_be_ops = {
	.startup = msm8996_quat_mi2s_snd_startup,
	.shutdown = msm8996_quat_mi2s_snd_shutdown,
};

static const struct soc_enum msm8996_mi2s_snd_enum[] = {
	SOC_ENUM_SINGLE_EXT(2, mi2s_bit_format_text),
	SOC_ENUM_SINGLE_EXT(6, mi2s_sample_rate_text),
	SOC_ENUM_SINGLE_EXT(4, pri_mi2s_rx_ch_text),
	SOC_ENUM_SINGLE_EXT(4, pri_mi2s_tx_ch_text),
	SOC_ENUM_SINGLE_EXT(4, sec_mi2s_rx_ch_text),
	SOC_ENUM_SINGLE_EXT(4, sec_mi2s_tx_ch_text),
	SOC_ENUM_SINGLE_EXT(4, tert_mi2s_rx_ch_text),
	SOC_ENUM_SINGLE_EXT(4, tert_mi2s_tx_ch_text),
	SOC_ENUM_SINGLE_EXT(8, quat_mi2s_rx_ch_text),
	SOC_ENUM_SINGLE_EXT(8, quat_mi2s_tx_ch_text),
};

static int msm_pri_mi2s_rx_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
					    struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_CHANNELS);

	param_set_mask(params, SNDRV_PCM_HW_PARAM_FORMAT,
				   pri_mi2s_bit_format);
	rate->min = rate->max = pri_mi2s_sample_rate;
	channels->min = channels->max = msm_pri_mi2s_rx_ch;

	 pr_debug("%s: format = %d, rate = %d, channels = %d\n",
		  __func__, params_format(params), params_rate(params),
		  msm_pri_mi2s_rx_ch);

	return 0;
}

static int msm_pri_mi2s_tx_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
					    struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_CHANNELS);

	param_set_mask(params, SNDRV_PCM_HW_PARAM_FORMAT,
				   pri_mi2s_bit_format);
	rate->min = rate->max = pri_mi2s_sample_rate;
	channels->min = channels->max = msm_pri_mi2s_tx_ch;

	 pr_debug("%s: format = %d, rate = %d, channels = %d\n",
		  __func__, params_format(params), params_rate(params),
		  msm_pri_mi2s_tx_ch);

	return 0;
}

static int msm_sec_mi2s_rx_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
					    struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_CHANNELS);

	param_set_mask(params, SNDRV_PCM_HW_PARAM_FORMAT,
				   sec_mi2s_bit_format);
	rate->min = rate->max = sec_mi2s_sample_rate;
	channels->min = channels->max = msm_sec_mi2s_rx_ch;

	 pr_debug("%s: format = %d, rate = %d, channels = %d\n",
		  __func__, params_format(params), params_rate(params),
		  msm_sec_mi2s_rx_ch);

	return 0;
}

static int msm_sec_mi2s_tx_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
					    struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_CHANNELS);

	param_set_mask(params, SNDRV_PCM_HW_PARAM_FORMAT,
				   sec_mi2s_bit_format);
	rate->min = rate->max = sec_mi2s_sample_rate;
	channels->min = channels->max = msm_sec_mi2s_tx_ch;

	 pr_debug("%s: format = %d, rate = %d, channels = %d\n",
		  __func__, params_format(params), params_rate(params),
		  msm_sec_mi2s_tx_ch);

	return 0;
}

static int msm_tert_mi2s_rx_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
					    struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_CHANNELS);

	param_set_mask(params, SNDRV_PCM_HW_PARAM_FORMAT,
				   tert_mi2s_bit_format);
	rate->min = rate->max = tert_mi2s_sample_rate;
	channels->min = channels->max = msm_tert_mi2s_rx_ch;

	 pr_debug("%s: format = %d, rate = %d, channels = %d\n",
		  __func__, params_format(params), params_rate(params),
		  msm_tert_mi2s_rx_ch);

	return 0;
}

#if 0
static int msm_tert_mi2s_tx_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
					    struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_CHANNELS);

	param_set_mask(params, SNDRV_PCM_HW_PARAM_FORMAT,
				   tert_mi2s_bit_format);
	rate->min = rate->max = tert_mi2s_sample_rate;
	channels->min = channels->max = msm_tert_mi2s_tx_ch;

	 pr_debug("%s: format = %d, rate = %d, channels = %d\n",
		  __func__, params_format(params), params_rate(params),
		  msm_tert_mi2s_tx_ch);

	return 0;
}
#endif

static int msm_quat_mi2s_rx_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
					    struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_CHANNELS);

	param_set_mask(params, SNDRV_PCM_HW_PARAM_FORMAT,
				   quat_mi2s_bit_format);
	rate->min = rate->max = quat_mi2s_sample_rate;
	channels->min = channels->max = msm_quat_mi2s_rx_ch;

	pr_debug("%s: format = %d, rate = %d, channels = %d\n",
		  __func__, params_format(params), params_rate(params),
		  msm_quat_mi2s_rx_ch);

	return 0;
}

static int msm_quat_mi2s_tx_be_hw_params_fixup(struct snd_soc_pcm_runtime *rtd,
					    struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_CHANNELS);

	param_set_mask(params, SNDRV_PCM_HW_PARAM_FORMAT,
				quat_mi2s_bit_format);
	rate->min = rate->max = quat_mi2s_sample_rate;
	channels->min = channels->max = msm_quat_mi2s_tx_ch;

	pr_debug("%s: format = %d, rate = %d, channels = %d\n",
		__func__, params_format(params), params_rate(params),
		msm_quat_mi2s_tx_ch);

	return 0;
}

static const struct soc_enum msm_snd_enum[] = {
	SOC_ENUM_SINGLE_EXT(2, spk_function),
	SOC_ENUM_SINGLE_EXT(2, slim0_rx_ch_text),
	SOC_ENUM_SINGLE_EXT(8, slim0_tx_ch_text),
	SOC_ENUM_SINGLE_EXT(7, hdmi_rx_ch_text),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(rx_bit_format_text),
			    rx_bit_format_text),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(slim0_rx_sample_rate_text),
			    slim0_rx_sample_rate_text),
	SOC_ENUM_SINGLE_EXT(8, proxy_rx_ch_text),
	SOC_ENUM_SINGLE_EXT(3, hdmi_rx_sample_rate_text),
	SOC_ENUM_SINGLE_EXT(4, slim5_rx_sample_rate_text),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(slim5_rx_bit_format_text),
			    slim5_rx_bit_format_text),
	SOC_ENUM_SINGLE_EXT(2, slim5_rx_ch_text),
	SOC_ENUM_SINGLE_EXT(2, hifi_function),
	SOC_ENUM_SINGLE_EXT(2, vi_feed_ch_text),
	SOC_ENUM_SINGLE_EXT(4, slim6_rx_sample_rate_text),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(slim6_rx_bit_format_text),
			    slim6_rx_bit_format_text),
	SOC_ENUM_SINGLE_EXT(2, slim6_rx_ch_text),
	SOC_ENUM_SINGLE_EXT(2, htc_ftm_mode), 
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(htc_as20_vol_index), htc_as20_vol_index), 
};

static const struct snd_kcontrol_new msm_snd_controls[] = {
	SOC_ENUM_EXT("Speaker Function", msm_snd_enum[0], msm8996_get_spk,
			msm8996_set_spk),
	SOC_ENUM_EXT("SLIM_0_RX Channels", msm_snd_enum[1],
			msm_slim_0_rx_ch_get, msm_slim_0_rx_ch_put),
	SOC_ENUM_EXT("SLIM_5_RX Channels", msm_snd_enum[10],
			msm_slim_5_rx_ch_get, msm_slim_5_rx_ch_put),
	SOC_ENUM_EXT("SLIM_6_RX Channels", msm_snd_enum[15],
			msm_slim_6_rx_ch_get, msm_slim_6_rx_ch_put),
	SOC_ENUM_EXT("SLIM_0_TX Channels", msm_snd_enum[2],
			msm_slim_0_tx_ch_get, msm_slim_0_tx_ch_put),
	SOC_ENUM_EXT("SLIM_1_TX Channels", msm_snd_enum[2],
			msm_slim_1_tx_ch_get, msm_slim_1_tx_ch_put),
	SOC_ENUM_EXT("AUX PCM SampleRate", msm8996_auxpcm_enum[0],
			msm8996_auxpcm_rate_get,
			msm8996_auxpcm_rate_put),
	SOC_ENUM_EXT("HDMI_RX Channels", msm_snd_enum[3],
			msm_hdmi_rx_ch_get, msm_hdmi_rx_ch_put),
	SOC_ENUM_EXT("SLIM_0_RX Format", msm_snd_enum[4],
			slim0_rx_bit_format_get, slim0_rx_bit_format_put),
	SOC_ENUM_EXT("SLIM_5_RX Format", msm_snd_enum[9],
			slim5_rx_bit_format_get, slim5_rx_bit_format_put),
	SOC_ENUM_EXT("SLIM_6_RX Format", msm_snd_enum[14],
			slim6_rx_bit_format_get, slim6_rx_bit_format_put),
	SOC_ENUM_EXT("SLIM_0_RX SampleRate", msm_snd_enum[5],
			slim0_rx_sample_rate_get, slim0_rx_sample_rate_put),
	SOC_ENUM_EXT("SLIM_5_RX SampleRate", msm_snd_enum[8],
			slim5_rx_sample_rate_get, slim5_rx_sample_rate_put),
	SOC_ENUM_EXT("SLIM_6_RX SampleRate", msm_snd_enum[13],
			slim6_rx_sample_rate_get, slim6_rx_sample_rate_put),
	SOC_ENUM_EXT("HDMI_RX Bit Format", msm_snd_enum[4],
			hdmi_rx_bit_format_get, hdmi_rx_bit_format_put),
	SOC_ENUM_EXT("PROXY_RX Channels", msm_snd_enum[6],
			msm_proxy_rx_ch_get, msm_proxy_rx_ch_put),
	SOC_ENUM_EXT("HDMI_RX SampleRate", msm_snd_enum[7],
			hdmi_rx_sample_rate_get, hdmi_rx_sample_rate_put),
	SOC_ENUM_EXT("PRI_MI2S BitWidth", msm8996_mi2s_snd_enum[0],
			pri_mi2s_bit_format_get, pri_mi2s_bit_format_put),
	SOC_ENUM_EXT("SEC_MI2S BitWidth", msm8996_mi2s_snd_enum[0],
			sec_mi2s_bit_format_get, sec_mi2s_bit_format_put),
	SOC_ENUM_EXT("TERT_MI2S BitWidth", msm8996_mi2s_snd_enum[0],
			tert_mi2s_bit_format_get, tert_mi2s_bit_format_put),
	SOC_ENUM_EXT("QUAT_MI2S BitWidth", msm8996_mi2s_snd_enum[0],
			quat_mi2s_bit_format_get, quat_mi2s_bit_format_put),
	SOC_ENUM_EXT("PRI_MI2S SampleRate", msm8996_mi2s_snd_enum[1],
			pri_mi2s_sample_rate_get, pri_mi2s_sample_rate_put),
	SOC_ENUM_EXT("SEC_MI2S SampleRate", msm8996_mi2s_snd_enum[1],
			sec_mi2s_sample_rate_get, sec_mi2s_sample_rate_put),
	SOC_ENUM_EXT("TERT_MI2S SampleRate", msm8996_mi2s_snd_enum[1],
			tert_mi2s_sample_rate_get, tert_mi2s_sample_rate_put),
	SOC_ENUM_EXT("QUAT_MI2S SampleRate", msm8996_mi2s_snd_enum[1],
			quat_mi2s_sample_rate_get, quat_mi2s_sample_rate_put),
	SOC_ENUM_EXT("PRI_MI2S_RX Channels", msm8996_mi2s_snd_enum[2],
			msm_pri_mi2s_rx_ch_get, msm_pri_mi2s_rx_ch_put),
	SOC_ENUM_EXT("PRI_MI2S_TX Channels", msm8996_mi2s_snd_enum[3],
			msm_pri_mi2s_tx_ch_get, msm_pri_mi2s_tx_ch_put),
	SOC_ENUM_EXT("SEC_MI2S_RX Channels", msm8996_mi2s_snd_enum[4],
			msm_sec_mi2s_rx_ch_get, msm_sec_mi2s_rx_ch_put),
	SOC_ENUM_EXT("SEC_MI2S_TX Channels", msm8996_mi2s_snd_enum[5],
			msm_sec_mi2s_tx_ch_get, msm_sec_mi2s_tx_ch_put),
	SOC_ENUM_EXT("TERT_MI2S_RX Channels", msm8996_mi2s_snd_enum[6],
			msm_tert_mi2s_rx_ch_get, msm_tert_mi2s_rx_ch_put),
	SOC_ENUM_EXT("TERT_MI2S_TX Channels", msm8996_mi2s_snd_enum[7],
			msm_tert_mi2s_tx_ch_get, msm_tert_mi2s_tx_ch_put),
	SOC_ENUM_EXT("QUAT_MI2S_RX Channels", msm8996_mi2s_snd_enum[8],
			msm_quat_mi2s_rx_ch_get, msm_quat_mi2s_rx_ch_put),
	SOC_ENUM_EXT("QUAT_MI2S_TX Channels", msm8996_mi2s_snd_enum[9],
			msm_quat_mi2s_tx_ch_get, msm_quat_mi2s_tx_ch_put),
	SOC_ENUM_EXT("SLIM_0_TX SampleRate", msm_snd_enum[5],
			slim0_tx_sample_rate_get, slim0_tx_sample_rate_put),
	SOC_ENUM_EXT("SLIM_0_TX Format", msm_snd_enum[4],
			slim0_tx_bit_format_get, slim0_tx_bit_format_put),
	SOC_ENUM_EXT("HiFi Function", msm_snd_enum[11], msm8996_hifi_get,
			msm8996_hifi_put),
	SOC_ENUM_EXT("VI_FEED_TX Channels", msm_snd_enum[12],
			msm_vi_feed_tx_ch_get, msm_vi_feed_tx_ch_put),
	SOC_ENUM_EXT("HTC_FTM_BT MODE", msm_snd_enum[16],
			msm_htc_ftm_get, msm_htc_ftm_put),
	SOC_ENUM_EXT("HTC_AS20_VOL Index", msm_snd_enum[17],
			msm_htc_as20_vol_index_get, msm_htc_as20_vol_index_put),
};

#ifdef CONFIG_USE_CODEC_MBHC
static bool msm8996_swap_gnd_mic(struct snd_soc_codec *codec)
{
	struct snd_soc_card *card = codec->component.card;
	struct msm8996_asoc_mach_data *pdata =
				snd_soc_card_get_drvdata(card);
	int value = gpio_get_value_cansleep(pdata->us_euro_gpio);

	pr_debug("%s: swap select switch %d to %d\n", __func__, value, !value);
	gpio_set_value_cansleep(pdata->us_euro_gpio, !value);
	return true;
}
#endif

static int msm_afe_set_config(struct snd_soc_codec *codec)
{
	int rc;
	void *config_data = NULL;

	pr_debug("%s: enter\n", __func__);

	if (!msm8996_codec_fn.get_afe_config_fn) {
		dev_err(codec->dev, "%s: codec get afe config not init'ed\n",
			__func__);
		return -EINVAL;
	}

	config_data = msm8996_codec_fn.get_afe_config_fn(codec,
			AFE_CDC_REGISTERS_CONFIG);
	if (config_data) {
		rc = afe_set_config(AFE_CDC_REGISTERS_CONFIG, config_data, 0);
		if (rc) {
			pr_err("%s: Failed to set codec registers config %d\n",
					__func__, rc);
			return rc;
		}
	}

	config_data = msm8996_codec_fn.get_afe_config_fn(codec,
			AFE_CDC_REGISTER_PAGE_CONFIG);
	if (config_data) {
		rc = afe_set_config(AFE_CDC_REGISTER_PAGE_CONFIG, config_data,
				    0);
		if (rc)
			pr_err("%s: Failed to set cdc register page config\n",
				__func__);
	}

	config_data = msm8996_codec_fn.get_afe_config_fn(codec,
			AFE_SLIMBUS_SLAVE_CONFIG);
	if (config_data) {
		rc = afe_set_config(AFE_SLIMBUS_SLAVE_CONFIG, config_data, 0);
		if (rc) {
			pr_err("%s: Failed to set slimbus slave config %d\n",
					__func__, rc);
			return rc;
		}
	}

	return 0;
}

static void msm_afe_clear_config(void)
{
	afe_clear_config(AFE_CDC_REGISTERS_CONFIG);
	afe_clear_config(AFE_SLIMBUS_SLAVE_CONFIG);
}

static int  msm8996_adsp_state_callback(struct notifier_block *nb,
					   unsigned long value, void *priv)
{
	if (value == SUBSYS_BEFORE_SHUTDOWN) {
		pr_debug("%s: ADSP is about to shutdown. Clearing AFE config\n",
			 __func__);
		msm_afe_clear_config();
	} else if (value == SUBSYS_AFTER_POWERUP) {
		pr_debug("%s: ADSP is up\n", __func__);
	}

	return NOTIFY_OK;
}

static struct notifier_block adsp_state_notifier_block = {
	.notifier_call = msm8996_adsp_state_callback,
	.priority = -INT_MAX,
};

static int msm8996_wcd93xx_codec_up(struct snd_soc_codec *codec)
{
	int err;
	unsigned long timeout;
	int adsp_ready = 0;

	timeout = jiffies +
		msecs_to_jiffies(ADSP_STATE_READY_TIMEOUT_MS);

	do {
		if (!q6core_is_adsp_ready()) {
			pr_err_ratelimited("%s: ADSP Audio isn't ready\n",
					   __func__);
			msleep(50);
		} else {
			pr_debug("%s: ADSP Audio is ready\n", __func__);
			adsp_ready = 1;
			break;
		}
	} while (time_after(timeout, jiffies));

	if (!adsp_ready) {
		pr_err("%s: timed out waiting for ADSP Audio\n", __func__);
		return -ETIMEDOUT;
	}

	err = msm_afe_set_config(codec);
	if (err)
		pr_err("%s: Failed to set AFE config. err %d\n",
			__func__, err);
	return err;
}

static int msm8996_tasha_codec_event_cb(struct snd_soc_codec *codec,
					enum wcd9335_codec_event codec_event)
{
	switch (codec_event) {
	case WCD9335_CODEC_EVENT_CODEC_UP:
		return msm8996_wcd93xx_codec_up(codec);
	default:
		pr_err("%s: UnSupported codec event %d\n",
			__func__, codec_event);
		return -EINVAL;
	}
}

#ifdef CONFIG_USE_CODEC_MBHC
static int msm8996_config_hph_en0_gpio(struct snd_soc_codec *codec, bool high)
{
	struct snd_soc_card *card = codec->component.card;
	struct msm8996_asoc_mach_data *pdata;
	int val;

	if (!card)
		return 0;

	pdata = snd_soc_card_get_drvdata(card);
	if (!pdata || !gpio_is_valid(pdata->hph_en0_gpio))
		return 0;

	val = gpio_get_value_cansleep(pdata->hph_en0_gpio);
	if ((!!val) == high)
		return 0;

	gpio_direction_output(pdata->hph_en0_gpio, (int)high);

	return 1;
}
#endif

static int htc_set_adaptivesound_effect(uint32_t value)
{
	int rc, i;
	char *params_value = NULL;
	int *update_params_value = NULL;
	uint32_t params_length = 0;

	mutex_lock(&htc_adaptivesound_enable_mutex);

	params_length = 4*sizeof(uint32_t) 
		+ 3*sizeof(uint32_t) + sizeof(uint) + ADAP_ONE_CHANNEL_SIZE 
		+ 4*sizeof(uint32_t) 
		+ 3*sizeof(uint32_t) + sizeof(short) + sizeof(ushort); 
	pr_debug("%s: value = %d, params_length = %d\n", __func__, value, params_length);

	params_value = kzalloc(params_length, GFP_KERNEL);
	if (!params_value) {
		pr_err("%s, params memory alloc failed", __func__);
		mutex_unlock(&htc_adaptivesound_enable_mutex);
		return -ENOMEM;
	}
	htc_adaptivesound_enable = value;

	update_params_value = (int *)params_value;
	pr_debug("%s: update_params_value 0x%p, length=%d\n", __func__, ( void * )update_params_value, params_length);

	
	*update_params_value++ = AFE_MODULE_ADAPTIVE_AUDIO_M1;
	*update_params_value++ = AFE_PARAM_ID_ADAPTIVE_AUDIO_M1_EN;
	*update_params_value++ = sizeof(uint32_t);
	*update_params_value++ = value;

	
	*update_params_value++ = AFE_MODULE_ADAPTIVE_AUDIO_M1;
	*update_params_value++ = AFE_PARAM_ID_ADAPTIVE_AUDIO_M1_CONF_L;
	*update_params_value++ = sizeof(uint) + ADAP_ONE_CHANNEL_SIZE;
	*update_params_value++ = ADAP_ONE_CHANNEL_CONF_NUM;
	for (i = 0; i < ADAP_ONE_CHANNEL_CONF_NUM_BY_DWORD; i++) {
		*update_params_value++ = g_AdapConfLchannel[i];
		if (i == 0 || i == ADAP_ONE_CHANNEL_CONF_NUM_BY_DWORD-1)
			pr_debug("%s: g_AdapConfLchannel[%d]=0x%x update_params_value=0x%x\n",
				__func__, i, g_AdapConfLchannel[i], *(update_params_value-1));
	}

	
	*update_params_value++ = AFE_MODULE_ADAPTIVE_AUDIO_M2;
	*update_params_value++ = AFE_PARAM_ID_ADAPTIVE_AUDIO_M2_EN;
	*update_params_value++ = sizeof(uint32_t);
	*update_params_value++ = value;

	
	pr_debug("%s, gain=%d\n", __func__, g_gain);
	*update_params_value++ = AFE_MODULE_ADAPTIVE_AUDIO_M2;
	*update_params_value++ = AFE_PARAM_ID_ADAPTIVE_AUDIO_M2_CONF;
	*update_params_value++ = sizeof(short) + sizeof(ushort);
	*update_params_value++ = (g_gain&0xFFFF)<<16 | 0;


	rc = htc_adm_effect_control(HTC_ADM_EFFECT_ADAPTIVEAUDIO_DATA1,
				AFE_PORT_ID_SLIMBUS_MULTI_CHAN_0_RX,
				AFE_COPP_ID_ADAPTIVE_AUDIO,
				params_length,
				params_value);

	if (rc)
		pr_err("%s: call q6adm_enable_effect to port 0x%x, rc %d\n",
			__func__,AFE_PORT_ID_SLIMBUS_MULTI_CHAN_0_RX,rc);

	memset(params_value, 0x0, params_length);
	update_params_value = (int *)params_value;
	params_length= 3*sizeof(uint32_t)+sizeof(uint) + ADAP_ONE_CHANNEL_SIZE;
	pr_debug("%s: update_params_value 0x%p, length=%d\n", __func__, (void *)update_params_value, params_length);

	
	*update_params_value++ = AFE_MODULE_ADAPTIVE_AUDIO_M1;
	*update_params_value++ = AFE_PARAM_ID_ADAPTIVE_AUDIO_M1_CONF_R;
	*update_params_value++ = sizeof(uint) + ADAP_ONE_CHANNEL_SIZE;
	*update_params_value++ = ADAP_ONE_CHANNEL_CONF_NUM;
	for (i = 0; i < ADAP_ONE_CHANNEL_CONF_NUM_BY_DWORD; i++) {
		*update_params_value++ = g_AdapConfRchannel[i];
		if (i == 0 || i == ADAP_ONE_CHANNEL_CONF_NUM_BY_DWORD-1)
			pr_debug("%s: g_AdapConfRchannel[%d]=0x%x update_params_value=0x%x\n",
				__func__, i, g_AdapConfRchannel[i], *(update_params_value-1));
	}

	rc = htc_adm_effect_control(HTC_ADM_EFFECT_ADAPTIVEAUDIO_DATA2,
				AFE_PORT_ID_SLIMBUS_MULTI_CHAN_0_RX,
				AFE_COPP_ID_ADAPTIVE_AUDIO,
				params_length,
				params_value);

	if (rc)
		pr_err("%s: call q6adm_enable_effect to port 0x%x, rc %d\n",
			__func__, AFE_PORT_ID_SLIMBUS_MULTI_CHAN_0_RX, rc);

	kfree(params_value);
	mutex_unlock(&htc_adaptivesound_enable_mutex);

	return rc;
}

static int msm_adaptivesound_Conf_Path(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	int i = 0, sz = 0;
	struct file *file = NULL;
	loff_t lofft = 0;

	for (i = 0; i < ((struct soc_multi_mixer_control *)kcontrol->private_value)->count; i++) {
		g_AdapConfFilePath[i] = ucontrol->value.integer.value[i];
	}
	g_AdapConfFilePath[((struct soc_multi_mixer_control *)kcontrol->private_value)->count-1] = '\0';
	pr_debug("[%s]path = %s\n", __func__, g_AdapConfFilePath);

	mutex_lock(&htc_adaptivesound_enable_mutex);

	if (*g_AdapConfFilePath)
		file = filp_open(g_AdapConfFilePath, O_RDONLY, 0);

	if (IS_ERR_OR_NULL(file)) { 
		pr_err("[%s]file %s open failed\n err=%ld", __func__, g_AdapConfFilePath, PTR_ERR(file));
		mutex_unlock(&htc_adaptivesound_enable_mutex);
		return -ENOENT;
	} else {
		sz = kernel_read(file, lofft, (char *)g_AdapConfLchannel, ADAP_ONE_CHANNEL_SIZE);
		lofft += ADAP_ONE_CHANNEL_SIZE;
		sz += kernel_read(file, lofft, (char *)g_AdapConfRchannel, ADAP_ONE_CHANNEL_SIZE);
		pr_debug("[%s]g_AdapConfLchannel[0] = 0x%x\n", __func__, g_AdapConfLchannel[0]);
		pr_debug("[%s]g_AdapConfRchannel[0] = 0x%x\n", __func__, g_AdapConfRchannel[0]);
		pr_debug("[%s]file %s size = %d\n", __func__, g_AdapConfFilePath, sz);
		filp_close(file, NULL);
	}

	mutex_unlock(&htc_adaptivesound_enable_mutex);

	return 0;
}

static int msm_adaptivesound_Conf_Gain_Path(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	int i = 0;
	struct file *file = NULL;
	loff_t lofft=0;

	for (i = 0; i < ((struct soc_multi_mixer_control *)kcontrol->private_value)->count; i++)
		g_AdapGainFilePath[i] = ucontrol->value.integer.value[i];
	g_AdapGainFilePath[((struct soc_multi_mixer_control *)kcontrol->private_value)->count-1] = '\0';
	pr_debug("[%s]path = %s", __func__, g_AdapGainFilePath);

	mutex_lock(&htc_adaptivesound_enable_mutex);

	if (*g_AdapGainFilePath)
		file = filp_open(g_AdapGainFilePath, O_RDONLY, 0);

	if (IS_ERR_OR_NULL(file)) { 
		pr_err("[%s]file %s open failed err=%ld\n", __func__, g_AdapGainFilePath, PTR_ERR(file));
		mutex_unlock(&htc_adaptivesound_enable_mutex);
		return -ENOENT;
	} else {
		kernel_read(file, lofft, (char *)&g_gain, 1);
		pr_debug("[%s]g_gain = %d\n", __func__, g_gain);
		filp_close(file, NULL);
	}

	mutex_unlock(&htc_adaptivesound_enable_mutex);
	return 0;
}

static int htc_set_onedotone_effect(uint32_t value)
{
    int rc;
    char *params_value;
    int *update_params_value;
    uint32_t params_length = 4*sizeof(uint32_t);

    pr_debug("%s: value = %d, params_length = %d\n", __func__, value, params_length);

    params_value = kzalloc(params_length, GFP_KERNEL);
    if (!params_value) {
        pr_err("%s, params memory alloc failed", __func__);
        return -ENOMEM;
    }

    htc_onedotone_enable = value;

    update_params_value = (int *)params_value;
    *update_params_value++ = AFE_MODULE_ONEDOTONE_AUDIO;
    *update_params_value++ = AFE_PARAM_ID_ONEDOTONE_AUDIO_EN;
    *update_params_value++ = sizeof(uint32_t);
    *update_params_value = value;

    rc = htc_adm_effect_control(HTC_ADM_EFFECT_ONEDOTONE,
                AFE_PORT_ID_QUATERNARY_MI2S_RX,
                AFE_COPP_ID_ONEDOTONE_AUDIO,
                params_length,
                params_value);
    if (rc) {
        pr_err("%s: call htc_adm_effect_control to port 0x%x, rc %d\n",
                __func__, AFE_PORT_ID_QUATERNARY_MI2S_RX, rc);
    }

    update_params_value = (int *)params_value;
    *update_params_value++ = AFE_MODULE_LIMITERCOPP;
    *update_params_value++ = AFE_PARAM_ID_LIMITERCOPP_AUDIO_ENABLE;
    *update_params_value++ = sizeof(uint32_t);
    *update_params_value = value;

    rc = htc_adm_effect_control(HTC_ADM_EFFECT_ONEDOTONE_LIMITER,
                AFE_PORT_ID_QUATERNARY_MI2S_RX,
                AFE_COPP_ID_ONEDOTONE_AUDIO,
                params_length,
                params_value);

    if (rc) {
        pr_err("%s: limiter control htc_adm_effect_control to port 0x%x, rc %d\n",
                __func__, AFE_PORT_ID_QUATERNARY_MI2S_RX, rc);
    }

    kfree(params_value);

    return rc;
}

static int msm_adaptivesound_enable_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	unsigned int enable_flag = 0;
	int rc = 0;

	pr_debug("%s, ucontrol->value.integer.value[0]=%d\n", __func__ ,(unsigned int)ucontrol->value.integer.value[0]);

	if ((ucontrol->value.integer.value[0] == 0) || (ucontrol->value.integer.value[0] == 1))
		enable_flag = ucontrol->value.integer.value[0];
	else
		return -EINVAL;

	rc = htc_set_adaptivesound_effect(enable_flag);

	return 0;
}

static int msm_adaptivesound_enable_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	ushort usEffect = get_adm_custom_effect_status();

	if ((usEffect & 0x1<<HTC_ADM_EFFECT_ADAPTIVEAUDIO_DATA1) &&
	    (usEffect & 0x1<<HTC_ADM_EFFECT_ADAPTIVEAUDIO_DATA2))
		ucontrol->value.integer.value[0] = 1;
	else
		ucontrol->value.integer.value[0] = 0;

	pr_debug("%s: adaptivesound_enable = %d 0x%x\n", __func__, htc_adaptivesound_enable, usEffect);
	return 0;
}

static int msm_onedotone_enable_put(struct snd_kcontrol *kcontrol,
                                            struct snd_ctl_elem_value *ucontrol)
{
    unsigned int enable_flag = 0;
    int rc = 0;

    pr_debug("%s, ucontrol->value.integer.value[0]=%d\n", __func__ ,(unsigned int)ucontrol->value.integer.value[0]);

    if ((ucontrol->value.integer.value[0] == 0) || (ucontrol->value.integer.value[0] == 1))
        enable_flag = ucontrol->value.integer.value[0];
    else
        return -EINVAL;

    rc = htc_set_onedotone_effect(enable_flag);

    return 0;
}

static int msm_onedotone_get(struct snd_kcontrol *kcontrol,
                                   struct snd_ctl_elem_value *ucontrol)
{
    ushort usEffect = get_adm_custom_effect_status();

    if (usEffect & (0x1<<HTC_ADM_EFFECT_ONEDOTONE))
        ucontrol->value.integer.value[0] = 1;
    else
        ucontrol->value.integer.value[0] = 0;

    pr_debug("%s: htc_onedotone_enable = %d 0x%x\n", __func__, htc_onedotone_enable, usEffect);
    return 0;
}

static int msm_admmiscvolramping_enable_put(struct snd_kcontrol *kcontrol,
                                            struct snd_ctl_elem_value *ucontrol)
{
    unsigned int enable_flag = 0;
    char *params_value = NULL;
    int *update_params_value = NULL;
    uint32_t params_length = 4*sizeof(uint32_t);

    pr_err("%s, ucontrol->value.integer.value[0]=%d\n", __func__ ,
            (unsigned int)ucontrol->value.integer.value[0]);

    if ((ucontrol->value.integer.value[0] == 0) || (ucontrol->value.integer.value[0] == 1))
        enable_flag = ucontrol->value.integer.value[0];
    else
        return -EINVAL;

    params_value = kzalloc(params_length, GFP_KERNEL);
    if (!params_value) {
        pr_err("%s, params memory alloc failed", __func__);
        return -ENOMEM;
    }

    update_params_value = (int *)params_value;
    *update_params_value++ = AFE_MODULE_ID_MISC_EFFECT;
    *update_params_value++ = AFE_PARAM_ID_MISC_SET_ACOUSTIC_SHOCK_RAMP;
    *update_params_value++ = sizeof(uint32_t);
    *update_params_value = enable_flag;

    htc_adm_effect_control(HTC_ADM_EFFECT_ONEDOTONE_RAMPING,
                AFE_PORT_ID_QUATERNARY_MI2S_RX,
                AFE_COPP_ID_ONEDOTONE_AUDIO,
                params_length,
                params_value);

    kfree(params_value);

    return 0;
}

static int msm_admmiscvolramping_get(struct snd_kcontrol *kcontrol,
                                   struct snd_ctl_elem_value *ucontrol)
{
    return 0;
}

static int msm_admmiscvolmute_enable_put(struct snd_kcontrol *kcontrol,
                                            struct snd_ctl_elem_value *ucontrol)
{
    unsigned int enable_flag = 0;
    char *params_value = NULL;
    int *update_params_value = NULL;
    uint32_t params_length = 4*sizeof(uint32_t);

    pr_err("%s, ucontrol->value.integer.value[0]=%d\n", __func__,
            (unsigned int)ucontrol->value.integer.value[0]);

    if ((ucontrol->value.integer.value[0] == 0) || (ucontrol->value.integer.value[0] == 1))
        enable_flag = ucontrol->value.integer.value[0];
    else
        return -EINVAL;

    params_value = kzalloc(params_length, GFP_KERNEL);
    if (!params_value) {
        pr_err("%s, params memory alloc failed", __func__);
        return -ENOMEM;
    }

    update_params_value = (int *)params_value;
    *update_params_value++ = AFE_MODULE_ID_MISC_EFFECT;
    *update_params_value++ = AFE_PARAM_ID_MISC_SET_ACOUSTIC_SHOCK_MUTE;
    *update_params_value++ = sizeof(uint32_t);
    *update_params_value = enable_flag;

    htc_adm_effect_control(HTC_ADM_EFFECT_ONEDOTONE_MUTE,
                AFE_PORT_ID_QUATERNARY_MI2S_RX,
                AFE_COPP_ID_ONEDOTONE_AUDIO,
                params_length,
                params_value);

    kfree(params_value);

    return 0;
}

static int msm_admmiscvolmute_get(struct snd_kcontrol *kcontrol,
                                   struct snd_ctl_elem_value *ucontrol)
{
    return 0;
}

static const struct snd_kcontrol_new htc_adaptivesound_params_control[] = {
    SOC_SINGLE_MULTI_EXT("AdaptiveSound CONFIG PATH", SND_SOC_NOPM,
    0, 0xFFFFFFFF, 0, 128, NULL, msm_adaptivesound_Conf_Path),

    SOC_SINGLE_MULTI_EXT("AdaptiveSound GAIN PATH", SND_SOC_NOPM,
    0, 0xFFFFFFFF, 0, 128, NULL, msm_adaptivesound_Conf_Gain_Path),

    SOC_SINGLE_EXT("AdaptiveSound Enable", SND_SOC_NOPM,
    0, 1, 0, msm_adaptivesound_enable_get, msm_adaptivesound_enable_put),
};

static const struct snd_kcontrol_new htc_onedotone_params_control[] = {
    SOC_SINGLE_EXT("OneDotOne Enable", SND_SOC_NOPM,
    0, 1, 0, msm_onedotone_get, msm_onedotone_enable_put),
};

static const struct snd_kcontrol_new htc_adm_misc_vol_params_control[] = {
    SOC_SINGLE_EXT("AdmMiscVolRamping", SND_SOC_NOPM,
    0, 1, 0, msm_admmiscvolramping_get, msm_admmiscvolramping_enable_put),
    SOC_SINGLE_EXT("AdmMiscVolMute", SND_SOC_NOPM,
    0, 1, 0, msm_admmiscvolmute_get, msm_admmiscvolmute_enable_put),
};

static int msm_audrx_init(struct snd_soc_pcm_runtime *rtd)
{
	int err;
	void *config_data;
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
#ifdef USE_WSA_AMP
	struct snd_soc_pcm_runtime *rtd_aux = rtd->card->rtd_aux;
#endif

#ifdef CONFIG_USE_CODEC_MBHC
	void *mbhc_calibration;
#endif
	struct snd_card *card;
	struct snd_info_entry *entry;
	struct msm8996_asoc_mach_data *pdata =
				snd_soc_card_get_drvdata(rtd->card);

	unsigned int rx_ch[TASHA_RX_MAX] = {144, 145, 146, 147, 148, 149, 150,
					    151, 152, 153, 154, 155, 156};
	unsigned int tx_ch[TASHA_TX_MAX] = {128, 129, 130, 131, 132, 133,
					    134, 135, 136, 137, 138, 139,
					    140, 141, 142, 143};

	pr_info("%s: dev_name%s\n", __func__, dev_name(cpu_dai->dev));

	rtd->pmdown_time = 0;

	err = snd_soc_add_codec_controls(codec, msm_snd_controls,
					 ARRAY_SIZE(msm_snd_controls));
	if (err < 0) {
		pr_err("%s: add_codec_controls failed, err %d\n",
			__func__, err);
		return err;
	}

	err = snd_soc_add_codec_controls(codec, htc_adaptivesound_params_control,
					 ARRAY_SIZE(htc_adaptivesound_params_control));
	if (err < 0) {
		pr_err("%s: add_codec_controls htc_adaptivesound_params_control failed, err %d\n",
			__func__, err);
		return err;
	}

	err = snd_soc_add_codec_controls(codec, htc_onedotone_params_control,
					 ARRAY_SIZE(htc_onedotone_params_control));
	if (err < 0) {
		pr_err("%s: add_codec_controls htc_onedotone_params_control failed, err %d\n",
			__func__, err);
		return err;
	}

	err = snd_soc_add_codec_controls(codec, htc_adm_misc_vol_params_control,
					 ARRAY_SIZE(htc_adm_misc_vol_params_control));
	if (err < 0) {
		pr_err("%s: add_codec_controls htc_adm_misc_vol_params_control failed, err %d\n",
			__func__, err);
		return err;
	}


	err = msm8996_liquid_init_docking();
	if (err) {
		pr_err("%s: 8996 init Docking stat IRQ failed (%d)\n",
			__func__, err);
		return err;
	}

	err = msm8996_ext_us_amp_init();
	if (err) {
		pr_err("%s: 8996 US Emitter GPIO init failed (%d)\n",
			__func__, err);
		return err;
	}

	snd_soc_dapm_new_controls(dapm, msm8996_dapm_widgets,
				ARRAY_SIZE(msm8996_dapm_widgets));

	snd_soc_dapm_add_routes(dapm, wcd9335_audio_paths,
				ARRAY_SIZE(wcd9335_audio_paths));
	snd_soc_dapm_enable_pin(dapm, "Lineout_1 amp");
	snd_soc_dapm_enable_pin(dapm, "Lineout_3 amp");
	snd_soc_dapm_enable_pin(dapm, "Lineout_2 amp");
	snd_soc_dapm_enable_pin(dapm, "Lineout_4 amp");

	snd_soc_dapm_ignore_suspend(dapm, "Lineout_1 amp");
	snd_soc_dapm_ignore_suspend(dapm, "Lineout_3 amp");
	snd_soc_dapm_ignore_suspend(dapm, "Lineout_2 amp");
	snd_soc_dapm_ignore_suspend(dapm, "Lineout_4 amp");
	snd_soc_dapm_ignore_suspend(dapm, "ultrasound amp");
	snd_soc_dapm_ignore_suspend(dapm, "Handset Mic");
	snd_soc_dapm_ignore_suspend(dapm, "Headset Mic");
	snd_soc_dapm_ignore_suspend(dapm, "ANCRight Headset Mic");
	snd_soc_dapm_ignore_suspend(dapm, "ANCLeft Headset Mic");
	snd_soc_dapm_ignore_suspend(dapm, "Digital Mic1");
	snd_soc_dapm_ignore_suspend(dapm, "Digital Mic2");
	snd_soc_dapm_ignore_suspend(dapm, "Digital Mic3");
	snd_soc_dapm_ignore_suspend(dapm, "Digital Mic4");
	snd_soc_dapm_ignore_suspend(dapm, "Digital Mic5");
	snd_soc_dapm_ignore_suspend(dapm, "Analog Mic4");
	snd_soc_dapm_ignore_suspend(dapm, "Analog Mic6");
	snd_soc_dapm_ignore_suspend(dapm, "Analog Mic7");
	snd_soc_dapm_ignore_suspend(dapm, "Analog Mic8");
	snd_soc_dapm_ignore_suspend(dapm, "MADINPUT");
	snd_soc_dapm_ignore_suspend(dapm, "MAD_CPE_INPUT");
	snd_soc_dapm_ignore_suspend(dapm, "EAR");
	snd_soc_dapm_ignore_suspend(dapm, "LINEOUT1");
	snd_soc_dapm_ignore_suspend(dapm, "LINEOUT2");
	snd_soc_dapm_ignore_suspend(dapm, "LINEOUT3");
	snd_soc_dapm_ignore_suspend(dapm, "LINEOUT4");
	snd_soc_dapm_ignore_suspend(dapm, "ANC EAR");
	snd_soc_dapm_ignore_suspend(dapm, "AMIC1");
	snd_soc_dapm_ignore_suspend(dapm, "AMIC2");
	snd_soc_dapm_ignore_suspend(dapm, "AMIC3");
	snd_soc_dapm_ignore_suspend(dapm, "AMIC4");
	snd_soc_dapm_ignore_suspend(dapm, "AMIC5");
	snd_soc_dapm_ignore_suspend(dapm, "AMIC6");
	snd_soc_dapm_ignore_suspend(dapm, "DMIC1");
	snd_soc_dapm_ignore_suspend(dapm, "DMIC2");
	snd_soc_dapm_ignore_suspend(dapm, "DMIC3");
	snd_soc_dapm_ignore_suspend(dapm, "DMIC4");
	snd_soc_dapm_ignore_suspend(dapm, "DMIC5");
	snd_soc_dapm_ignore_suspend(dapm, "Digital Mic0");
	snd_soc_dapm_ignore_suspend(dapm, "DMIC0");
	snd_soc_dapm_ignore_suspend(dapm, "SPK1 OUT");
	snd_soc_dapm_ignore_suspend(dapm, "SPK2 OUT");
	snd_soc_dapm_ignore_suspend(dapm, "HPHL");
	snd_soc_dapm_ignore_suspend(dapm, "HPHR");
	snd_soc_dapm_ignore_suspend(dapm, "ANC HPHL");
	snd_soc_dapm_ignore_suspend(dapm, "ANC HPHR");
	snd_soc_dapm_ignore_suspend(dapm, "ANC LINEOUT1");
	snd_soc_dapm_ignore_suspend(dapm, "ANC LINEOUT2");
	snd_soc_dapm_ignore_suspend(dapm, "AIF4 VI");
	snd_soc_dapm_ignore_suspend(dapm, "VIINPUT");
	snd_soc_dapm_ignore_suspend(dapm, "Digital Mic");

	snd_soc_dapm_sync(dapm);

	snd_soc_dai_set_channel_map(codec_dai, ARRAY_SIZE(tx_ch),
				    tx_ch, ARRAY_SIZE(rx_ch), rx_ch);
	msm8996_codec_fn.get_afe_config_fn = tasha_get_afe_config;
#ifdef CONFIG_USE_CODEC_MBHC
	msm8996_codec_fn.mbhc_hs_detect_exit = tasha_mbhc_hs_detect_exit;
#endif

	err = msm_afe_set_config(codec);
	if (err) {
		pr_err("%s: Failed to set AFE config %d\n", __func__, err);
		goto out;
	}

	config_data = msm8996_codec_fn.get_afe_config_fn(codec,
						AFE_AANC_VERSION);
	if (config_data) {
		err = afe_set_config(AFE_AANC_VERSION, config_data, 0);
		if (err) {
			pr_err("%s: Failed to set aanc version %d\n",
				__func__, err);
			goto out;
		}
	}
	config_data = msm8996_codec_fn.get_afe_config_fn(codec,
					    AFE_CDC_CLIP_REGISTERS_CONFIG);
	if (config_data) {
		err = afe_set_config(AFE_CDC_CLIP_REGISTERS_CONFIG,
				     config_data, 0);
		if (err) {
			pr_err("%s: Failed to set clip registers %d\n",
				__func__, err);
			goto out;
		}
	}
	config_data = msm8996_codec_fn.get_afe_config_fn(codec,
			AFE_CLIP_BANK_SEL);
	if (config_data) {
		err = afe_set_config(AFE_CLIP_BANK_SEL, config_data, 0);
		if (err) {
			pr_err("%s: Failed to set AFE bank selection %d\n",
				__func__, err);
			goto out;
		}
	}
	
#ifdef CONFIG_USE_CODEC_MBHC
	tasha_mbhc_zdet_gpio_ctrl(msm8996_config_hph_en0_gpio, rtd->codec);
	mbhc_calibration = def_tasha_mbhc_cal();
	if (mbhc_calibration) {
		wcd_mbhc_cfg.calibration = mbhc_calibration;
		err = tasha_mbhc_hs_detect(codec, &wcd_mbhc_cfg);
		if (err) {
			pr_err("%s: mbhc hs detect failed, err:%d\n",
				__func__, err);
			goto out;
		}
	} else {
		pr_err("%s: mbhc_cfg calibration is NULL\n", __func__);
		err = -ENOMEM;
		goto out;
	}
#endif
	adsp_state_notifier = subsys_notif_register_notifier("adsp",
						&adsp_state_notifier_block);
	if (!adsp_state_notifier) {
		pr_err("%s: Failed to register adsp state notifier\n",
		       __func__);
		err = -EFAULT;
#ifdef CONFIG_USE_CODEC_MBHC
		msm8996_codec_fn.mbhc_hs_detect_exit(codec);
#endif
		goto out;
	}

	tasha_event_register(msm8996_tasha_codec_event_cb, rtd->codec);

#ifdef USE_WSA_AMP
	if (rtd_aux && rtd_aux->component)
		if (!strcmp(rtd_aux->component->name, WSA8810_NAME_1) ||
		    !strcmp(rtd_aux->component->name, WSA8810_NAME_2)) {
			tasha_set_spkr_mode(rtd->codec, SPKR_MODE_1);
			tasha_set_spkr_gain_offset(rtd->codec,
						   RX_GAIN_OFFSET_M1P5_DB);
	}
#endif

	codec_reg_done = true;

	card = rtd->card->snd_card;
	entry = snd_register_module_info(card->module, "codecs",
					 card->proc_root);
	if (!entry) {
		pr_debug("%s: Cannot create codecs module entry\n",
			 __func__);
		err = 0;
		goto out;
	}
	pdata->codec_root = entry;
	tasha_codec_info_create_codec_entry(pdata->codec_root, codec);

	return 0;
out:
	return err;
}

#ifdef CONFIG_USE_CODEC_MBHC
static void *def_tasha_mbhc_cal(void)
{
	void *tasha_wcd_cal;
	struct wcd_mbhc_btn_detect_cfg *btn_cfg;
	u16 *btn_high;

	tasha_wcd_cal = kzalloc(WCD_MBHC_CAL_SIZE(WCD_MBHC_DEF_BUTTONS,
				WCD9XXX_MBHC_DEF_RLOADS), GFP_KERNEL);
	if (!tasha_wcd_cal) {
		pr_err("%s: out of memory\n", __func__);
		return NULL;
	}

#define S(X, Y) ((WCD_MBHC_CAL_PLUG_TYPE_PTR(tasha_wcd_cal)->X) = (Y))
	S(v_hs_max, 1600); 
#undef S
#define S(X, Y) ((WCD_MBHC_CAL_BTN_DET_PTR(tasha_wcd_cal)->X) = (Y))
	S(num_btn, WCD_MBHC_DEF_BUTTONS);
#undef S

	btn_cfg = WCD_MBHC_CAL_BTN_DET_PTR(tasha_wcd_cal);
	btn_high = ((void *)&btn_cfg->_v_btn_low) +
		(sizeof(btn_cfg->_v_btn_low[0]) * btn_cfg->num_btn);

	btn_high[0] = 88;
	btn_high[1] = 213;
	btn_high[2] = 438;
	btn_high[3] = 438;
	btn_high[4] = 438;
	btn_high[5] = 438;
	btn_high[6] = 438;
	btn_high[7] = 438;

	return tasha_wcd_cal;
}
#endif

static int msm_snd_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai_link *dai_link = rtd->dai_link;

	int ret = 0;
	u32 rx_ch[SLIM_MAX_RX_PORTS], tx_ch[SLIM_MAX_TX_PORTS];
	u32 rx_ch_cnt = 0, tx_ch_cnt = 0;
	u32 user_set_tx_ch = 0;
	u32 rx_ch_count;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		ret = snd_soc_dai_get_channel_map(codec_dai,
					&tx_ch_cnt, tx_ch, &rx_ch_cnt , rx_ch);
		if (ret < 0) {
			pr_err("%s: failed to get codec chan map, err:%d\n",
				__func__, ret);
			goto end;
		}
		if (dai_link->be_id == MSM_BACKEND_DAI_SLIMBUS_5_RX) {
			pr_debug("%s: rx_5_ch=%d\n", __func__,
				  msm_slim_5_rx_ch);
			rx_ch_count = msm_slim_5_rx_ch;
		} else if (dai_link->be_id == MSM_BACKEND_DAI_SLIMBUS_6_RX) {
			pr_debug("%s: rx_6_ch=%d\n", __func__,
				  msm_slim_6_rx_ch);
			rx_ch_count = msm_slim_6_rx_ch;
		} else {
			pr_debug("%s: rx_0_ch=%d\n", __func__,
				  msm_slim_0_rx_ch);
			rx_ch_count = msm_slim_0_rx_ch;
		}
		ret = snd_soc_dai_set_channel_map(cpu_dai, 0, 0,
						  rx_ch_count, rx_ch);
		if (ret < 0) {
			pr_err("%s: failed to set cpu chan map, err:%d\n",
				__func__, ret);
			goto end;
		}
	} else {

		pr_debug("%s: %s_tx_dai_id_%d_ch=%d\n", __func__,
			 codec_dai->name, codec_dai->id, user_set_tx_ch);
		ret = snd_soc_dai_get_channel_map(codec_dai,
					 &tx_ch_cnt, tx_ch, &rx_ch_cnt , rx_ch);
		if (ret < 0) {
			pr_err("%s: failed to get codec chan map\n, err:%d\n",
				__func__, ret);
			goto end;
		}
		
		if (dai_link->be_id == MSM_BACKEND_DAI_SLIMBUS_0_TX)
			user_set_tx_ch = msm_slim_0_tx_ch;
		
		else if (dai_link->be_id == MSM_BACKEND_DAI_SLIMBUS_1_TX)
			user_set_tx_ch = msm_slim_1_tx_ch;
		else if (dai_link->be_id == MSM_BACKEND_DAI_SLIMBUS_3_TX)
			user_set_tx_ch = msm_slim_0_rx_ch;
		else if (dai_link->be_id == MSM_BACKEND_DAI_SLIMBUS_4_TX)
			user_set_tx_ch = msm_vi_feed_tx_ch;
		else
			user_set_tx_ch = tx_ch_cnt;

		pr_debug("%s: msm_slim_0_tx_ch(%d) user_set_tx_ch(%d) tx_ch_cnt(%d), be_id (%d)\n",
			 __func__, msm_slim_0_tx_ch, user_set_tx_ch,
			 tx_ch_cnt, dai_link->be_id);

		ret = snd_soc_dai_set_channel_map(cpu_dai,
						  user_set_tx_ch, tx_ch, 0 , 0);
		if (ret < 0) {
			pr_err("%s: failed to set cpu chan map, err:%d\n",
				__func__, ret);
			goto end;
		}
	}
end:
	return ret;
}

static int msm_snd_cpe_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai_link *dai_link = rtd->dai_link;

	int ret = 0;
	u32 tx_ch[SLIM_MAX_TX_PORTS];
	u32 tx_ch_cnt = 0;
	u32 user_set_tx_ch = 0;

	if (substream->stream != SNDRV_PCM_STREAM_CAPTURE) {
		pr_err("%s: Invalid stream type %d\n",
			__func__, substream->stream);
		ret = -EINVAL;
		goto end;
	}

	pr_debug("%s: %s_tx_dai_id_%d\n", __func__,
		 codec_dai->name, codec_dai->id);
	ret = snd_soc_dai_get_channel_map(codec_dai,
				 &tx_ch_cnt, tx_ch, NULL , NULL);
	if (ret < 0) {
		pr_err("%s: failed to get codec chan map\n, err:%d\n",
			__func__, ret);
		goto end;
	}

	user_set_tx_ch = tx_ch_cnt;

	pr_debug("%s: tx_ch_cnt(%d) be_id %d\n",
		 __func__, tx_ch_cnt, dai_link->be_id);

	ret = snd_soc_dai_set_channel_map(cpu_dai,
					  user_set_tx_ch, tx_ch, 0 , 0);
	if (ret < 0)
		pr_err("%s: failed to set cpu chan map, err:%d\n",
			__func__, ret);
end:
	return ret;
}

static struct snd_soc_ops msm8996_be_ops = {
	.hw_params = msm_snd_hw_params,
};

static struct snd_soc_ops msm8996_cpe_ops = {
	.hw_params = msm_snd_cpe_hw_params,
};

static int msm8996_slimbus_2_hw_params(struct snd_pcm_substream *substream,
					  struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int ret = 0;
	unsigned int rx_ch[SLIM_MAX_RX_PORTS], tx_ch[SLIM_MAX_TX_PORTS];
	unsigned int rx_ch_cnt = 0, tx_ch_cnt = 0;
	unsigned int num_tx_ch = 0;
	unsigned int num_rx_ch = 0;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		num_rx_ch =  params_channels(params);
		pr_debug("%s: %s rx_dai_id = %d  num_ch = %d\n", __func__,
			codec_dai->name, codec_dai->id, num_rx_ch);
		ret = snd_soc_dai_get_channel_map(codec_dai,
				&tx_ch_cnt, tx_ch, &rx_ch_cnt , rx_ch);
		if (ret < 0) {
			pr_err("%s: failed to get codec chan map, err:%d\n",
				__func__, ret);
			goto end;
		}
		ret = snd_soc_dai_set_channel_map(cpu_dai, 0, 0,
				num_rx_ch, rx_ch);
		if (ret < 0) {
			pr_err("%s: failed to set cpu chan map, err:%d\n",
				__func__, ret);
			goto end;
		}
	} else {
		num_tx_ch =  params_channels(params);
		pr_debug("%s: %s  tx_dai_id = %d  num_ch = %d\n", __func__,
			codec_dai->name, codec_dai->id, num_tx_ch);
		ret = snd_soc_dai_get_channel_map(codec_dai,
				&tx_ch_cnt, tx_ch, &rx_ch_cnt , rx_ch);
		if (ret < 0) {
			pr_err("%s: failed to get codec chan map, err:%d\n",
				__func__, ret);
			goto end;
		}
		ret = snd_soc_dai_set_channel_map(cpu_dai,
				num_tx_ch, tx_ch, 0 , 0);
		if (ret < 0) {
			pr_err("%s: failed to set cpu chan map, err:%d\n",
				__func__, ret);
			goto end;
		}
	}
end:
	return ret;
}

static struct snd_soc_ops msm8996_slimbus_2_be_ops = {
	.hw_params = msm8996_slimbus_2_hw_params,
};

static int msm8996_get_ll_qos_val(struct snd_pcm_runtime *runtime)
{
	int usecs;

	
	usecs = (100000 / runtime->rate) * runtime->period_size;
	usecs += ((100000 % runtime->rate) * runtime->period_size) /
		runtime->rate;

	return usecs;
}

static int msm8996_mm5_prepare(struct snd_pcm_substream *substream)
{
	if (pm_qos_request_active(&substream->latency_pm_qos_req))
		pm_qos_remove_request(&substream->latency_pm_qos_req);
	pm_qos_add_request(&substream->latency_pm_qos_req,
			   PM_QOS_CPU_DMA_LATENCY,
			   msm8996_get_ll_qos_val(substream->runtime));
	return 0;
}

static struct snd_soc_ops msm8996_mm5_ops = {
	.prepare = msm8996_mm5_prepare,
};

static struct snd_soc_dai_link msm8996_common_dai_links[] = {
	
	{
		.name = "MSM8996 Media1",
		.stream_name = "MultiMedia1",
		.cpu_dai_name = "MultiMedia1",
		.platform_name = "msm-pcm-dsp.0",
		.dynamic = 1,
		.async_ops = ASYNC_DPCM_SND_SOC_PREPARE,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		
		.ignore_pmdown_time = 1,
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA1
	},
	{
		.name = "MSM8996 Media2",
		.stream_name = "MultiMedia2",
		.cpu_dai_name = "MultiMedia2",
		.platform_name = "msm-pcm-dsp.0",
		.dynamic = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.ignore_suspend = 1,
		
		.ignore_pmdown_time = 1,
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA2,
	},
	{
		.name = "VoiceMMode1",
		.stream_name = "VoiceMMode1",
		.cpu_dai_name = "VoiceMMode1",
		.platform_name = "msm-pcm-voice",
		.dynamic = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.be_id = MSM_FRONTEND_DAI_VOICEMMODE1,
	},
	{
		.name = "MSM VoIP",
		.stream_name = "VoIP",
		.cpu_dai_name = "VoIP",
		.platform_name = "msm-voip-dsp",
		.dynamic = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		
		.ignore_pmdown_time = 1,
		.be_id = MSM_FRONTEND_DAI_VOIP,
	},
	{
		.name = "MSM8996 ULL",
		.stream_name = "MultiMedia3",
		.cpu_dai_name = "MultiMedia3",
		.platform_name = "msm-pcm-dsp.2",
		.dynamic = 1,
		.async_ops = ASYNC_DPCM_SND_SOC_PREPARE,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		
		.ignore_pmdown_time = 1,
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA3,
	},
	
	{
		.name = "SLIMBUS_0 Hostless",
		.stream_name = "SLIMBUS_0 Hostless",
		.cpu_dai_name = "SLIMBUS0_HOSTLESS",
		.platform_name = "msm-pcm-hostless",
		.dynamic = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		 
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
	},
	{
		.name = "Tertiary MI2S TX_Hostless",
		.stream_name = "Tertiary MI2S_TX Hostless Capture",
		.cpu_dai_name = "snd-soc-dummy-dai",
		.platform_name = "msm-pcm-hostless",
		.dynamic = 1,
		.dpcm_capture = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
	},
	{
		.name = "MSM AFE-PCM RX",
		.stream_name = "AFE-PROXY RX",
		.cpu_dai_name = "msm-dai-q6-dev.241",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-rx",
		.platform_name = "msm-pcm-afe",
		.ignore_suspend = 1,
		
		.ignore_pmdown_time = 1,
	},
	{
		.name = "MSM AFE-PCM TX",
		.stream_name = "AFE-PROXY TX",
		.cpu_dai_name = "msm-dai-q6-dev.240",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-tx",
		.platform_name  = "msm-pcm-afe",
		.ignore_suspend = 1,
	},
	{
		.name = "MSM8996 Compress1",
		.stream_name = "Compress1",
		.cpu_dai_name = "MultiMedia4",
		.platform_name = "msm-compress-dsp",
		.dynamic = 1,
		.async_ops = ASYNC_DPCM_SND_SOC_HW_PARAMS,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			 SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		 
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA4,
	},
	{
		.name = "AUXPCM Hostless",
		.stream_name = "AUXPCM Hostless",
		.cpu_dai_name = "AUXPCM_HOSTLESS",
		.platform_name = "msm-pcm-hostless",
		.dynamic = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
	},
	{
		.name = "SLIMBUS_1 Hostless",
		.stream_name = "SLIMBUS_1 Hostless",
		.cpu_dai_name = "SLIMBUS1_HOSTLESS",
		.platform_name = "msm-pcm-hostless",
		.dynamic = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		 
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
	},
	{
		.name = "SLIMBUS_3 Hostless",
		.stream_name = "SLIMBUS_3 Hostless",
		.cpu_dai_name = "SLIMBUS3_HOSTLESS",
		.platform_name = "msm-pcm-hostless",
		.dynamic = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		 
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
	},
	{
		.name = "SLIMBUS_4 Hostless",
		.stream_name = "SLIMBUS_4 Hostless",
		.cpu_dai_name = "SLIMBUS4_HOSTLESS",
		.platform_name = "msm-pcm-hostless",
		.dynamic = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		 
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
	},
	{
		.name = "VoLTE",
		.stream_name = "VoLTE",
		.cpu_dai_name = "VoLTE",
		.platform_name = "msm-pcm-voice",
		.dynamic = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.be_id = MSM_FRONTEND_DAI_VOLTE,
	},
	{
		.name = "MSM8996 LowLatency",
		.stream_name = "MultiMedia5",
		.cpu_dai_name = "MultiMedia5",
		.platform_name = "msm-pcm-dsp.1",
		.dynamic = 1,
		.async_ops = ASYNC_DPCM_SND_SOC_PREPARE,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
				SND_SOC_DPCM_TRIGGER_POST},
		.ignore_suspend = 1,
		
		.ignore_pmdown_time = 1,
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA5,
		.ops = &msm8996_mm5_ops,
	},
	{
		.name = "Listen 1 Audio Service",
		.stream_name = "Listen 1 Audio Service",
		.cpu_dai_name = "LSM1",
		.platform_name = "msm-lsm-client",
		.dynamic = 1,
		.dpcm_capture = 1,
		.trigger = { SND_SOC_DPCM_TRIGGER_POST,
			     SND_SOC_DPCM_TRIGGER_POST },
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.be_id = MSM_FRONTEND_DAI_LSM1,
	},
	
	{
		.name = "MSM8996 Compress2",
		.stream_name = "Compress2",
		.cpu_dai_name = "MultiMedia7",
		.platform_name = "msm-compress-dsp",
		.dynamic = 1,
		.dpcm_playback = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			 SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		 
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA7,
	},
	{
		.name = "MSM8996 Compress3",
		.stream_name = "Compress3",
		.cpu_dai_name = "MultiMedia10",
		.platform_name = "msm-compress-dsp",
		.dynamic = 1,
		.dpcm_playback = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			 SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		 
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA10,
	},
	{
		.name = "MSM8996 ULL NOIRQ",
		.stream_name = "MM_NOIRQ",
		.cpu_dai_name = "MultiMedia8",
		.platform_name = "msm-pcm-dsp-noirq",
		.dynamic = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			 SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		 
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA8,
	},
	{
		.name = "QCHAT",
		.stream_name = "QCHAT",
		.cpu_dai_name = "QCHAT",
		.platform_name = "msm-pcm-voice",
		.dynamic = 1,
		.dpcm_capture = 1,
		.dpcm_playback = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.be_id = MSM_FRONTEND_DAI_QCHAT,
	},
	
	{
		.name = "HDMI_RX_HOSTLESS",
		.stream_name = "HDMI_RX_HOSTLESS",
		.cpu_dai_name = "HDMI_HOSTLESS",
		.platform_name = "msm-pcm-hostless",
		.dynamic = 1,
		.dpcm_playback = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
	},
	{
		.name = "VoiceMMode2",
		.stream_name = "VoiceMMode2",
		.cpu_dai_name = "VoiceMMode2",
		.platform_name = "msm-pcm-voice",
		.dynamic = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.be_id = MSM_FRONTEND_DAI_VOICEMMODE2,
	},
	{
		.name = "INT_HFP_BT Hostless",
		.stream_name = "INT_HFP_BT Hostless",
		.cpu_dai_name = "INT_HFP_BT_HOSTLESS",
		.platform_name = "msm-pcm-hostless",
		.dynamic = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
	},
	{
		.name = "MSM8996 HFP TX",
		.stream_name = "MultiMedia6",
		.cpu_dai_name = "MultiMedia6",
		.platform_name = "msm-pcm-loopback",
		.dynamic = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.ignore_suspend = 1,
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		
		.ignore_pmdown_time = 1,
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA6,
	},
	
	{
		.name = "Listen 2 Audio Service",
		.stream_name = "Listen 2 Audio Service",
		.cpu_dai_name = "LSM2",
		.platform_name = "msm-lsm-client",
		.dynamic = 1,
		.dpcm_capture = 1,
		.trigger = { SND_SOC_DPCM_TRIGGER_POST,
				 SND_SOC_DPCM_TRIGGER_POST },
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.be_id = MSM_FRONTEND_DAI_LSM2,
	},
	{
		.name = "Listen 3 Audio Service",
		.stream_name = "Listen 3 Audio Service",
		.cpu_dai_name = "LSM3",
		.platform_name = "msm-lsm-client",
		.dynamic = 1,
		.dpcm_capture = 1,
		.trigger = { SND_SOC_DPCM_TRIGGER_POST,
				 SND_SOC_DPCM_TRIGGER_POST },
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.be_id = MSM_FRONTEND_DAI_LSM3,
	},
	{
		.name = "Listen 4 Audio Service",
		.stream_name = "Listen 4 Audio Service",
		.cpu_dai_name = "LSM4",
		.platform_name = "msm-lsm-client",
		.dynamic = 1,
		.dpcm_capture = 1,
		.trigger = { SND_SOC_DPCM_TRIGGER_POST,
				 SND_SOC_DPCM_TRIGGER_POST },
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.be_id = MSM_FRONTEND_DAI_LSM4,
	},
	{
		.name = "Listen 5 Audio Service",
		.stream_name = "Listen 5 Audio Service",
		.cpu_dai_name = "LSM5",
		.platform_name = "msm-lsm-client",
		.dynamic = 1,
		.dpcm_capture = 1,
		.trigger = { SND_SOC_DPCM_TRIGGER_POST,
				 SND_SOC_DPCM_TRIGGER_POST },
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.be_id = MSM_FRONTEND_DAI_LSM5,
	},
	{
		.name = "Listen 6 Audio Service",
		.stream_name = "Listen 6 Audio Service",
		.cpu_dai_name = "LSM6",
		.platform_name = "msm-lsm-client",
		.dynamic = 1,
		.dpcm_capture = 1,
		.trigger = { SND_SOC_DPCM_TRIGGER_POST,
				 SND_SOC_DPCM_TRIGGER_POST },
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.be_id = MSM_FRONTEND_DAI_LSM6,
	},
	{
		.name = "Listen 7 Audio Service",
		.stream_name = "Listen 7 Audio Service",
		.cpu_dai_name = "LSM7",
		.platform_name = "msm-lsm-client",
		.dynamic = 1,
		.dpcm_capture = 1,
		.trigger = { SND_SOC_DPCM_TRIGGER_POST,
				 SND_SOC_DPCM_TRIGGER_POST },
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.be_id = MSM_FRONTEND_DAI_LSM7,
	},
	{
		.name = "Listen 8 Audio Service",
		.stream_name = "Listen 8 Audio Service",
		.cpu_dai_name = "LSM8",
		.platform_name = "msm-lsm-client",
		.dynamic = 1,
		.dpcm_capture = 1,
		.trigger = { SND_SOC_DPCM_TRIGGER_POST,
				 SND_SOC_DPCM_TRIGGER_POST },
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.be_id = MSM_FRONTEND_DAI_LSM8,
	},
	{
		.name = "MSM8996 Media9",
		.stream_name = "MultiMedia9",
		.cpu_dai_name = "MultiMedia9",
		.platform_name = "msm-pcm-dsp.0",
		.dynamic = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
				SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		
		.ignore_pmdown_time = 1,
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA9,
	},
	{
		.name = "VoWLAN",
		.stream_name = "VoWLAN",
		.cpu_dai_name = "VoWLAN",
		.platform_name = "msm-pcm-voice",
		.dynamic = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.be_id = MSM_FRONTEND_DAI_VOWLAN,
	},
	{
		.name = "MSM8996 Compress4",
		.stream_name = "Compress4",
		.cpu_dai_name = "MultiMedia11",
		.platform_name = "msm-compress-dsp",
		.dynamic = 1,
		.dpcm_playback = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			 SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		 
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA11,
	},
	{
		.name = "MSM8996 Compress5",
		.stream_name = "Compress5",
		.cpu_dai_name = "MultiMedia12",
		.platform_name = "msm-compress-dsp",
		.dynamic = 1,
		.dpcm_playback = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			 SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		 
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA12,
	},
	{
		.name = "MSM8996 Compress6",
		.stream_name = "Compress6",
		.cpu_dai_name = "MultiMedia13",
		.platform_name = "msm-compress-dsp",
		.dynamic = 1,
		.dpcm_playback = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			 SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		 
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA13,
	},
	{
		.name = "MSM8996 Compress7",
		.stream_name = "Compress7",
		.cpu_dai_name = "MultiMedia14",
		.platform_name = "msm-compress-dsp",
		.dynamic = 1,
		.dpcm_playback = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			 SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		 
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA14,
	},
	{
		.name = "MSM8996 Compress8",
		.stream_name = "Compress8",
		.cpu_dai_name = "MultiMedia15",
		.platform_name = "msm-compress-dsp",
		.dynamic = 1,
		.dpcm_playback = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			 SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		 
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA15,
	},
	{
		.name = "MSM8996 Compress9",
		.stream_name = "Compress9",
		.cpu_dai_name = "MultiMedia16",
		.platform_name = "msm-compress-dsp",
		.dynamic = 1,
		.dpcm_playback = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			 SND_SOC_DPCM_TRIGGER_POST},
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		 
		.be_id = MSM_FRONTEND_DAI_MULTIMEDIA16,
	},
	{
		.name = "Circuit-Switch Voice",
		.stream_name = "CS-Voice",
		.cpu_dai_name   = "CS-VOICE",
		.platform_name  = "msm-pcm-voice",
		.dynamic = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		
		.ignore_pmdown_time = 1,
		.be_id = MSM_FRONTEND_DAI_CS_VOICE,
	},
	{
		.name = "Voice2",
		.stream_name = "Voice2",
		.cpu_dai_name = "Voice2",
		.platform_name = "msm-pcm-voice",
		.dynamic = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.be_id = MSM_FRONTEND_DAI_VOICE2,
	},
	{
		.name = "Primary MI2S RX_Hostless",
		.stream_name = "Primary MI2S_RX Hostless Playback",
		.cpu_dai_name = "PRI_MI2S_RX_HOSTLESS",
		.platform_name = "msm-pcm-hostless",
		.dynamic = 1,
		.dpcm_playback = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
	},
	{
		.name = "Primary MI2S TX_Hostless",
		.stream_name = "Primary MI2S_TX Hostless Capture",
		.cpu_dai_name = "PRI_MI2S_TX_HOSTLESS",
		.platform_name = "msm-pcm-hostless",
		.dynamic = 1,
		.dpcm_capture = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
	},
	{
		.name = "Secondary MI2S RX_Hostless",
		.stream_name = "Secondary MI2S_RX Hostless Playback",
		.cpu_dai_name = "snd-soc-dummy-dai",
		.platform_name = "msm-pcm-hostless",
		.dynamic = 1,
		.dpcm_playback = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
	},
	{
		.name = "Secondary MI2S TX_Hostless",
		.stream_name = "Secondary MI2S_TX Hostless Capture",
		.cpu_dai_name = "snd-soc-dummy-dai",
		.platform_name = "msm-pcm-hostless",
		.dynamic = 1,
		.dpcm_capture = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
	},
	{
		.name = "Tertiary MI2S RX_Hostless",
		.stream_name = "Tertiary MI2S_RX Hostless Playback",
		.cpu_dai_name = "snd-soc-dummy-dai",
		.platform_name = "msm-pcm-hostless",
		.dynamic = 1,
		.dpcm_playback = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
	},
	{
		.name = "Quaternary MI2S RX_Hostless",
		.stream_name = "Quaternary MI2S_RX Hostless Playback",
		.cpu_dai_name = "QUAT_MI2S_RX_HOSTLESS",
		.platform_name = "msm-pcm-hostless",
		.dynamic = 1,
		.dpcm_playback = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
	},
	{
		.name = "Quaternary MI2S TX_Hostless",
		.stream_name = "Quaternary MI2S_TX Hostless Capture",
		.cpu_dai_name = "QUAT_MI2S_TX_HOSTLESS",
		.platform_name = "msm-pcm-hostless",
		.dynamic = 1,
		.dpcm_capture = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
	},
};

static struct snd_soc_dai_link msm8996_tasha_fe_dai_links[] = {
	{
		.name = LPASS_BE_SLIMBUS_4_TX,
		.stream_name = "Slimbus4 Capture",
		.cpu_dai_name = "msm-dai-q6-dev.16393",
		.platform_name = "msm-pcm-hostless",
		.codec_name = "tasha_codec",
		.codec_dai_name = "tasha_vifeedback",
		.be_id = MSM_BACKEND_DAI_SLIMBUS_4_TX,
		.be_hw_params_fixup = msm_slim_4_tx_be_hw_params_fixup,
		.ops = &msm8996_be_ops,
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
	},
	
	{
		.name = "SLIMBUS_2 Hostless Playback",
		.stream_name = "SLIMBUS_2 Hostless Playback",
		.cpu_dai_name = "msm-dai-q6-dev.16388",
		.platform_name = "msm-pcm-hostless",
		.codec_name = "tasha_codec",
		.codec_dai_name = "tasha_rx2",
		.ignore_suspend = 1,
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ops = &msm8996_slimbus_2_be_ops,
	},
	
	{
		.name = "SLIMBUS_2 Hostless Capture",
		.stream_name = "SLIMBUS_2 Hostless Capture",
		.cpu_dai_name = "msm-dai-q6-dev.16389",
		.platform_name = "msm-pcm-hostless",
		.codec_name = "tasha_codec",
		.codec_dai_name = "tasha_tx2",
		.ignore_suspend = 1,
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ops = &msm8996_slimbus_2_be_ops,
	},
	
	{
		.name = "CPE Listen service",
		.stream_name = "CPE Listen Audio Service",
		.cpu_dai_name = "msm-dai-slim",
		.platform_name = "msm-cpe-lsm",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "tasha_mad1",
		.codec_name = "tasha_codec",
		.ops = &msm8996_cpe_ops,
	},
	
	{
		.name = "SLIMBUS_6 Hostless Playback",
		.stream_name = "SLIMBUS_6 Hostless",
		.cpu_dai_name = "SLIMBUS6_HOSTLESS",
		.platform_name = "msm-pcm-hostless",
		.dynamic = 1,
		.dpcm_playback = 1,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		 
		.ignore_pmdown_time = 1,
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
	},
	
	{
		.name = "CPE Listen service ECPP",
		.stream_name = "CPE Listen Audio Service ECPP",
		.cpu_dai_name = "CPE_LSM_NOHOST",
		.platform_name = "msm-cpe-lsm.3",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST,
			    SND_SOC_DPCM_TRIGGER_POST},
		.no_host_mode = SND_SOC_DAI_LINK_NO_HOST,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.codec_dai_name = "tasha_cpe",
		.codec_name = "tasha_codec",
	},
};

static struct snd_soc_dai_link msm8996_common_be_dai_links[] = {
	
	{
		.name = LPASS_BE_AFE_PCM_RX,
		.stream_name = "AFE Playback",
		.cpu_dai_name = "msm-dai-q6-dev.224",
		.platform_name = "msm-pcm-routing",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-rx",
		.no_pcm = 1,
		.dpcm_playback = 1,
		.be_id = MSM_BACKEND_DAI_AFE_PCM_RX,
		.be_hw_params_fixup = msm_proxy_rx_be_hw_params_fixup,
		
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
	},
	{
		.name = LPASS_BE_AFE_PCM_TX,
		.stream_name = "AFE Capture",
		.cpu_dai_name = "msm-dai-q6-dev.225",
		.platform_name = "msm-pcm-routing",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-tx",
		.no_pcm = 1,
		.dpcm_capture = 1,
		.be_id = MSM_BACKEND_DAI_AFE_PCM_TX,
		.be_hw_params_fixup = msm_proxy_tx_be_hw_params_fixup,
		.ignore_suspend = 1,
	},
	
	{
		.name = LPASS_BE_AUXPCM_RX,
		.stream_name = "AUX PCM Playback",
		.cpu_dai_name = "msm-dai-q6-auxpcm.1",
		.platform_name = "msm-pcm-routing",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-rx",
		.no_pcm = 1,
		.dpcm_playback = 1,
		.be_id = MSM_BACKEND_DAI_AUXPCM_RX,
		.be_hw_params_fixup = msm_auxpcm_be_params_fixup,
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
		
	},
	{
		.name = LPASS_BE_AUXPCM_TX,
		.stream_name = "AUX PCM Capture",
		.cpu_dai_name = "msm-dai-q6-auxpcm.1",
		.platform_name = "msm-pcm-routing",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-tx",
		.no_pcm = 1,
		.dpcm_capture = 1,
		.be_id = MSM_BACKEND_DAI_AUXPCM_TX,
		.be_hw_params_fixup = msm_auxpcm_be_params_fixup,
		.ignore_suspend = 1,
	},
	
	{
		.name = LPASS_BE_INCALL_RECORD_TX,
		.stream_name = "Voice Uplink Capture",
		.cpu_dai_name = "msm-dai-q6-dev.32772",
		.platform_name = "msm-pcm-routing",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-tx",
		.no_pcm = 1,
		.dpcm_capture = 1,
		.be_id = MSM_BACKEND_DAI_INCALL_RECORD_TX,
		.be_hw_params_fixup = msm_be_hw_params_fixup,
		.ignore_suspend = 1,
	},
	
	{
		.name = LPASS_BE_INCALL_RECORD_RX,
		.stream_name = "Voice Downlink Capture",
		.cpu_dai_name = "msm-dai-q6-dev.32771",
		.platform_name = "msm-pcm-routing",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-tx",
		.no_pcm = 1,
		.dpcm_capture = 1,
		.be_id = MSM_BACKEND_DAI_INCALL_RECORD_RX,
		.be_hw_params_fixup = msm_be_hw_params_fixup,
		.ignore_suspend = 1,
	},
	
	{
		.name = LPASS_BE_VOICE_PLAYBACK_TX,
		.stream_name = "Voice Farend Playback",
		.cpu_dai_name = "msm-dai-q6-dev.32773",
		.platform_name = "msm-pcm-routing",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-rx",
		.no_pcm = 1,
		.dpcm_playback = 1,
		.be_id = MSM_BACKEND_DAI_VOICE_PLAYBACK_TX,
		.be_hw_params_fixup = msm_be_hw_params_fixup,
		.ignore_suspend = 1,
	},
	
	{
		.name = LPASS_BE_VOICE2_PLAYBACK_TX,
		.stream_name = "Voice2 Farend Playback",
		.cpu_dai_name = "msm-dai-q6-dev.32770",
		.platform_name = "msm-pcm-routing",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-rx",
		.no_pcm = 1,
		.dpcm_playback = 1,
		.be_id = MSM_BACKEND_DAI_VOICE2_PLAYBACK_TX,
		.be_hw_params_fixup = msm_be_hw_params_fixup,
		.ignore_suspend = 1,
	},
	{
		.name = LPASS_BE_TERT_MI2S_TX,
		.stream_name = "Tertiary MI2S Capture",
		.cpu_dai_name = "snd-soc-dummy-dai",
		.platform_name = "msm-pcm-routing",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.no_pcm = 1,
		.dpcm_capture = 1,
		.be_id = MSM_BACKEND_DAI_TERTIARY_MI2S_TX,
		.be_hw_params_fixup = msm_tx_be_hw_params_fixup,
		.ops = &legacy_msm8996_mi2s_be_ops,
		.ignore_suspend = 1,
	},
	{
		.name = LPASS_BE_PRI_MI2S_RX,
		.stream_name = "Primary MI2S Playback",
		.cpu_dai_name = "msm-dai-q6-mi2s.0",
		.platform_name = "msm-pcm-routing",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.no_pcm = 1,
		.dpcm_playback = 1,
		.be_id = MSM_BACKEND_DAI_PRI_MI2S_RX,
		.be_hw_params_fixup = msm_pri_mi2s_rx_be_hw_params_fixup,
		.ops = &msm8996_pri_mi2s_be_ops,
		.ignore_suspend = 1,
	},
	{
		.name = LPASS_BE_PRI_MI2S_TX,
		.stream_name = "Primary MI2S Capture",
		.cpu_dai_name = "msm-dai-q6-mi2s.0",
		.platform_name = "msm-pcm-routing",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.no_pcm = 1,
		.dpcm_capture = 1,
		.be_id = MSM_BACKEND_DAI_PRI_MI2S_TX,
		.be_hw_params_fixup = msm_pri_mi2s_tx_be_hw_params_fixup,
		.ops = &msm8996_pri_mi2s_be_ops,
		.ignore_suspend = 1,
	},
	{
		.name = LPASS_BE_SEC_MI2S_RX,
		.stream_name = "Secondary MI2S Playback",
		.cpu_dai_name = "snd-soc-dummy-dai",
		.platform_name = "msm-pcm-routing",
#ifdef CONFIG_RT_REGMAP
		.codec_name = "rt5503.7-0052",
		.codec_dai_name = "rt5503-aif1",
#else
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
#endif
		.no_pcm = 1,
		.dpcm_playback = 1,
		.be_id = MSM_BACKEND_DAI_SECONDARY_MI2S_RX,
		.be_hw_params_fixup = msm_sec_mi2s_rx_be_hw_params_fixup,
		.ops = &msm8996_sec_mi2s_be_ops,
		.ignore_suspend = 1,
	},
	{
		.name = LPASS_BE_SEC_MI2S_TX,
		.stream_name = "Secondary MI2S Capture",
		.cpu_dai_name = "snd-soc-dummy-dai",
		.platform_name = "msm-pcm-routing",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.no_pcm = 1,
		.dpcm_capture = 1,
		.be_id = MSM_BACKEND_DAI_SECONDARY_MI2S_TX,
		.be_hw_params_fixup = msm_sec_mi2s_tx_be_hw_params_fixup,
		.ops = &msm8996_sec_mi2s_be_ops,
		.ignore_suspend = 1,
	},
	{
		.name = LPASS_BE_TERT_MI2S_RX,
		.stream_name = "Tertiary MI2S Playback",
		.cpu_dai_name = "snd-soc-dummy-dai",
		.platform_name = "msm-pcm-routing",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.no_pcm = 1,
		.dpcm_playback = 1,
		.be_id = MSM_BACKEND_DAI_TERTIARY_MI2S_RX,
		.be_hw_params_fixup = msm_tert_mi2s_rx_be_hw_params_fixup,
		.ops = &msm8996_tert_mi2s_be_ops,
		.ignore_suspend = 1,
	},
	{
		.name = LPASS_BE_QUAT_MI2S_RX,
		.stream_name = "Quaternary MI2S Playback",
		.cpu_dai_name = "msm-dai-q6-mi2s.3",
		.platform_name = "msm-pcm-routing",
#ifdef CONFIG_SND_SOC_TFA98XX
		.codec_name = "tfa-codec",
		.codec_dai_name = "tfa98xx-aif",
#else
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
#endif
		.no_pcm = 1,
		.dpcm_playback = 1,
		.be_id = MSM_BACKEND_DAI_QUATERNARY_MI2S_RX,
		.be_hw_params_fixup = msm_quat_mi2s_rx_be_hw_params_fixup,
		.ops = &msm8996_quat_mi2s_be_ops,
		.ignore_suspend = 1,
	},
	{
		.name = LPASS_BE_QUAT_MI2S_TX,
		.stream_name = "Quaternary MI2S Capture",
		.cpu_dai_name = "msm-dai-q6-mi2s.3",
		.platform_name = "msm-pcm-routing",
		.codec_name = "msm-stub-codec.1",
		.codec_dai_name = "msm-stub-tx",
		.no_pcm = 1,
		.dpcm_capture = 1,
		.be_id = MSM_BACKEND_DAI_QUATERNARY_MI2S_TX,
		.be_hw_params_fixup = msm_quat_mi2s_tx_be_hw_params_fixup,
		.ops = &msm8996_quat_mi2s_be_ops,
		.ignore_suspend = 1,
	}
};

static struct snd_soc_dai_link msm8996_tasha_be_dai_links[] = {
	
	{
		.name = LPASS_BE_SLIMBUS_0_RX,
		.stream_name = "Slimbus Playback",
		.cpu_dai_name = "msm-dai-q6-dev.16384",
		.platform_name = "msm-pcm-routing",
		.codec_name = "tasha_codec",
		.codec_dai_name = "tasha_mix_rx1",
		.no_pcm = 1,
		.dpcm_playback = 1,
		.be_id = MSM_BACKEND_DAI_SLIMBUS_0_RX,
		.init = &msm_audrx_init,
		.be_hw_params_fixup = msm_slim_0_rx_be_hw_params_fixup,
		
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
		.ops = &msm8996_be_ops,
	},
	{
		.name = LPASS_BE_SLIMBUS_0_TX,
		.stream_name = "Slimbus Capture",
		.cpu_dai_name = "msm-dai-q6-dev.16385",
		.platform_name = "msm-pcm-routing",
		.codec_name = "tasha_codec",
		.codec_dai_name = "tasha_tx1",
		.no_pcm = 1,
		.dpcm_capture = 1,
		.be_id = MSM_BACKEND_DAI_SLIMBUS_0_TX,
		.be_hw_params_fixup = msm_slim_0_tx_be_hw_params_fixup,
		.ignore_suspend = 1,
		.ops = &msm8996_be_ops,
	},
	{
		.name = LPASS_BE_SLIMBUS_1_RX,
		.stream_name = "Slimbus1 Playback",
		.cpu_dai_name = "msm-dai-q6-dev.16386",
		.platform_name = "msm-pcm-routing",
		.codec_name = "tasha_codec",
		.codec_dai_name = "tasha_mix_rx1",
		.no_pcm = 1,
		.dpcm_playback = 1,
		.be_id = MSM_BACKEND_DAI_SLIMBUS_1_RX,
		.be_hw_params_fixup = msm_slim_0_rx_be_hw_params_fixup,
		.ops = &msm8996_be_ops,
		
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
	},
	{
		.name = LPASS_BE_SLIMBUS_1_TX,
		.stream_name = "Slimbus1 Capture",
		.cpu_dai_name = "msm-dai-q6-dev.16387",
		.platform_name = "msm-pcm-routing",
		.codec_name = "tasha_codec",
		.codec_dai_name = "tasha_tx3",
		.no_pcm = 1,
		.dpcm_capture = 1,
		.be_id = MSM_BACKEND_DAI_SLIMBUS_1_TX,
		.be_hw_params_fixup = msm_slim_1_tx_be_hw_params_fixup,
		.ops = &msm8996_be_ops,
		.ignore_suspend = 1,
	},
	{
		.name = LPASS_BE_SLIMBUS_3_RX,
		.stream_name = "Slimbus3 Playback",
		.cpu_dai_name = "msm-dai-q6-dev.16390",
		.platform_name = "msm-pcm-routing",
		.codec_name = "tasha_codec",
		.codec_dai_name = "tasha_mix_rx1",
		.no_pcm = 1,
		.dpcm_playback = 1,
		.be_id = MSM_BACKEND_DAI_SLIMBUS_3_RX,
		.be_hw_params_fixup = msm_slim_0_rx_be_hw_params_fixup,
		.ops = &msm8996_be_ops,
		
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
	},
	{
		.name = LPASS_BE_SLIMBUS_3_TX,
		.stream_name = "Slimbus3 Capture",
		.cpu_dai_name = "msm-dai-q6-dev.16391",
		.platform_name = "msm-pcm-routing",
		.codec_name = "tasha_codec",
		.codec_dai_name = "tasha_tx1",
		.no_pcm = 1,
		.dpcm_capture = 1,
		.be_id = MSM_BACKEND_DAI_SLIMBUS_3_TX,
		.be_hw_params_fixup = msm_slim_0_tx_be_hw_params_fixup,
		.ops = &msm8996_be_ops,
		.ignore_suspend = 1,
	},
	{
		.name = LPASS_BE_SLIMBUS_4_RX,
		.stream_name = "Slimbus4 Playback",
		.cpu_dai_name = "msm-dai-q6-dev.16392",
		.platform_name = "msm-pcm-routing",
		.codec_name = "tasha_codec",
		.codec_dai_name = "tasha_mix_rx1",
		.no_pcm = 1,
		.dpcm_playback = 1,
		.be_id = MSM_BACKEND_DAI_SLIMBUS_4_RX,
		.be_hw_params_fixup = msm_slim_0_rx_be_hw_params_fixup,
		.ops = &msm8996_be_ops,
		
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
	},
	{
		.name = LPASS_BE_SLIMBUS_5_RX,
		.stream_name = "Slimbus5 Playback",
		.cpu_dai_name = "msm-dai-q6-dev.16394",
		.platform_name = "msm-pcm-routing",
		.codec_name = "tasha_codec",
		.codec_dai_name = "tasha_rx3",
		.no_pcm = 1,
		.dpcm_playback = 1,
		.be_id = MSM_BACKEND_DAI_SLIMBUS_5_RX,
		.be_hw_params_fixup = msm_slim_5_rx_be_hw_params_fixup,
		.ops = &msm8996_be_ops,
		
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
	},
	
	{
		.name = LPASS_BE_SLIMBUS_5_TX,
		.stream_name = "Slimbus5 Capture",
		.cpu_dai_name = "msm-dai-q6-dev.16395",
		.platform_name = "msm-pcm-routing",
		.codec_name = "tasha_codec",
		.codec_dai_name = "tasha_mad1",
		.no_pcm = 1,
		.dpcm_capture = 1,
		.be_id = MSM_BACKEND_DAI_SLIMBUS_5_TX,
		.be_hw_params_fixup = msm_slim_5_tx_be_hw_params_fixup,
		.ops = &msm8996_be_ops,
		.ignore_suspend = 1,
	},
	{
		.name = LPASS_BE_SLIMBUS_6_RX,
		.stream_name = "Slimbus6 Playback",
		.cpu_dai_name = "msm-dai-q6-dev.16396",
		.platform_name = "msm-pcm-routing",
		.codec_name = "tasha_codec",
		.codec_dai_name = "tasha_rx4",
		.no_pcm = 1,
		.dpcm_playback = 1,
		.be_id = MSM_BACKEND_DAI_SLIMBUS_6_RX,
		.be_hw_params_fixup = msm_slim_6_rx_be_hw_params_fixup,
		.ops = &msm8996_be_ops,
		
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
	},
};

static struct snd_soc_dai_link msm8996_hdmi_dai_link[] = {
	
	{
		.name = LPASS_BE_HDMI,
		.stream_name = "HDMI Playback",
		.cpu_dai_name = "msm-dai-q6-hdmi.8",
		.platform_name = "msm-pcm-routing",
		.codec_name = "msm-hdmi-audio-codec-rx",
		.codec_dai_name = "msm_hdmi_audio_codec_rx_dai",
		.no_pcm = 1,
		.dpcm_playback = 1,
		.be_id = MSM_BACKEND_DAI_HDMI_RX,
		.be_hw_params_fixup = msm8996_hdmi_be_hw_params_fixup,
		.ignore_pmdown_time = 1,
		.ignore_suspend = 1,
	},
};

static struct snd_soc_dai_link msm8996_tasha_dai_links[
			 ARRAY_SIZE(msm8996_common_dai_links) +
			 ARRAY_SIZE(msm8996_tasha_fe_dai_links) +
			 ARRAY_SIZE(msm8996_common_be_dai_links) +
			 ARRAY_SIZE(msm8996_tasha_be_dai_links) +
			 ARRAY_SIZE(msm8996_hdmi_dai_link)];

#ifdef USE_WSA_AMP
static int msm8996_wsa881x_init(struct snd_soc_component *component)
{
	u8 spkleft_ports[WSA881X_MAX_SWR_PORTS] = {100, 101, 102, 106};
	u8 spkright_ports[WSA881X_MAX_SWR_PORTS] = {103, 104, 105, 107};
	unsigned int ch_rate[WSA881X_MAX_SWR_PORTS] = {2400, 600, 300, 1200};
	unsigned int ch_mask[WSA881X_MAX_SWR_PORTS] = {0x1, 0xF, 0x3, 0x3};
	struct snd_soc_codec *codec = snd_soc_component_to_codec(component);
	struct msm8996_asoc_mach_data *pdata;
	struct snd_soc_dapm_context *dapm;

	if (!codec) {
		pr_err("%s codec is NULL\n", __func__);
		return -EINVAL;
	}

	dapm = &codec->dapm;

	if (!strcmp(component->name_prefix, "SpkrLeft")) {
		dev_dbg(codec->dev, "%s: setting left ch map to codec %s\n",
			__func__, codec->component.name);
		wsa881x_set_channel_map(codec, &spkleft_ports[0],
				WSA881X_MAX_SWR_PORTS, &ch_mask[0],
				&ch_rate[0]);
		if (dapm->component) {
			snd_soc_dapm_ignore_suspend(dapm, "SpkrLeft IN");
			snd_soc_dapm_ignore_suspend(dapm, "SpkrLeft SPKR");
		}
	} else if (!strcmp(component->name_prefix, "SpkrRight")) {
		dev_dbg(codec->dev, "%s: setting right ch map to codec %s\n",
			__func__, codec->component.name);
		wsa881x_set_channel_map(codec, &spkright_ports[0],
				WSA881X_MAX_SWR_PORTS, &ch_mask[0],
				&ch_rate[0]);
		if (dapm->component) {
			snd_soc_dapm_ignore_suspend(dapm, "SpkrRight IN");
			snd_soc_dapm_ignore_suspend(dapm, "SpkrRight SPKR");
		}
	} else {
		dev_err(codec->dev, "%s: wrong codec name %s\n", __func__,
			codec->component.name);
		return -EINVAL;
	}
	pdata = snd_soc_card_get_drvdata(component->card);
	if (pdata && pdata->codec_root)
		wsa881x_codec_info_create_codec_entry(pdata->codec_root,
						      codec);

	return 0;
}
#endif

struct snd_soc_card snd_soc_card_tasha_msm8996 = {
	.name		= "msm8996-tasha-snd-card",
};

static int msm8996_populate_dai_link_component_of_node(
					struct snd_soc_card *card)
{
	int i, index, ret = 0;
	struct device *cdev = card->dev;
	struct snd_soc_dai_link *dai_link = card->dai_link;
	struct device_node *np;

	if (!cdev) {
		pr_err("%s: Sound card device memory NULL\n", __func__);
		return -ENODEV;
	}

	for (i = 0; i < card->num_links; i++) {
		if (dai_link[i].platform_of_node && dai_link[i].cpu_of_node)
			continue;

		
		if (dai_link[i].platform_name &&
		    !dai_link[i].platform_of_node) {
			index = of_property_match_string(cdev->of_node,
						"asoc-platform-names",
						dai_link[i].platform_name);
			if (index < 0) {
				pr_err("%s: No match found for platform name: %s\n",
					__func__, dai_link[i].platform_name);
				ret = index;
				goto err;
			}
			np = of_parse_phandle(cdev->of_node, "asoc-platform",
					      index);
			if (!np) {
				pr_err("%s: retrieving phandle for platform %s, index %d failed\n",
					__func__, dai_link[i].platform_name,
					index);
				ret = -ENODEV;
				goto err;
			}
			dai_link[i].platform_of_node = np;
			dai_link[i].platform_name = NULL;
		}

		
		if (dai_link[i].cpu_dai_name && !dai_link[i].cpu_of_node) {
			index = of_property_match_string(cdev->of_node,
						 "asoc-cpu-names",
						 dai_link[i].cpu_dai_name);
			if (index >= 0) {
				np = of_parse_phandle(cdev->of_node, "asoc-cpu",
						index);
				if (!np) {
					pr_err("%s: retrieving phandle for cpu dai %s failed\n",
						__func__,
						dai_link[i].cpu_dai_name);
					ret = -ENODEV;
					goto err;
				}
				dai_link[i].cpu_of_node = np;
				dai_link[i].cpu_dai_name = NULL;
			}
		}

		
		if (dai_link[i].codec_name && !dai_link[i].codec_of_node) {
			index = of_property_match_string(cdev->of_node,
						 "asoc-codec-names",
						 dai_link[i].codec_name);
			if (index < 0)
				continue;
			np = of_parse_phandle(cdev->of_node, "asoc-codec",
					      index);
			if (!np) {
				pr_err("%s: retrieving phandle for codec %s failed\n",
					__func__, dai_link[i].codec_name);
				ret = -ENODEV;
				goto err;
			}
			dai_link[i].codec_of_node = np;
			dai_link[i].codec_name = NULL;
		}
	}

err:
	return ret;
}

static int msm8996_prepare_us_euro(struct snd_soc_card *card)
{
	struct msm8996_asoc_mach_data *pdata =
				snd_soc_card_get_drvdata(card);
	int ret;
	if (pdata->us_euro_gpio >= 0) {
		dev_dbg(card->dev, "%s: us_euro gpio request %d", __func__,
			pdata->us_euro_gpio);
		ret = gpio_request(pdata->us_euro_gpio, "TASHA_CODEC_US_EURO");
		if (ret) {
			dev_err(card->dev,
				"%s: Failed to request codec US/EURO gpio %d error %d\n",
				__func__, pdata->us_euro_gpio, ret);
			return ret;
		}
	}

	return 0;
}

static int msm8996_prepare_hifi(struct snd_soc_card *card)
{
	struct msm8996_asoc_mach_data *pdata =
				snd_soc_card_get_drvdata(card);
	int ret;

	if (gpio_is_valid(pdata->hph_en1_gpio)) {
		dev_dbg(card->dev, "%s: hph_en1_gpio request %d\n", __func__,
			pdata->hph_en1_gpio);
		ret = gpio_request(pdata->hph_en1_gpio, "hph_en1_gpio");
		if (ret) {
			dev_err(card->dev,
				"%s: hph_en1_gpio request failed, ret:%d\n",
				__func__, ret);
			return ret;
		}
	}
	if (gpio_is_valid(pdata->hph_en0_gpio)) {
		dev_dbg(card->dev, "%s: hph_en0_gpio request %d\n", __func__,
			pdata->hph_en0_gpio);
		ret = gpio_request(pdata->hph_en0_gpio, "hph_en0_gpio");
		if (ret) {
			dev_err(card->dev,
				"%s: hph_en0_gpio request failed, ret:%d\n",
				__func__, ret);
			return ret;
		}
	}
	return 0;
}

static const struct of_device_id msm8996_asoc_machine_of_match[]  = {
	{ .compatible = "qcom,msm8996-asoc-snd-tasha",
	  .data = "tasha_codec"},
	{},
};

static struct snd_soc_card *populate_snd_card_dailinks(struct device *dev)
{
	struct snd_soc_card *card = NULL;
	struct snd_soc_dai_link *dailink = NULL; 
	int len_1 = 0, len_2 = 0, len_3 = 0, len_4 = 0; 
	const struct of_device_id *match;

	match = of_match_node(msm8996_asoc_machine_of_match, dev->of_node);
	if (!match) {
		dev_err(dev, "%s: No DT match found for sound card\n",
			__func__);
		return NULL;
	}

	if (!strcmp(match->data, "tasha_codec")) {
		card = &snd_soc_card_tasha_msm8996;
		len_1 = ARRAY_SIZE(msm8996_common_dai_links);
		len_2 = len_1 + ARRAY_SIZE(msm8996_tasha_fe_dai_links);
		len_3 = len_2 + ARRAY_SIZE(msm8996_common_be_dai_links);

		memcpy(msm8996_tasha_dai_links,
		       msm8996_common_dai_links,
		       sizeof(msm8996_common_dai_links));
		memcpy(msm8996_tasha_dai_links + len_1,
		       msm8996_tasha_fe_dai_links,
		       sizeof(msm8996_tasha_fe_dai_links));
		memcpy(msm8996_tasha_dai_links + len_2,
		       msm8996_common_be_dai_links,
		       sizeof(msm8996_common_be_dai_links));
		memcpy(msm8996_tasha_dai_links + len_3,
		       msm8996_tasha_be_dai_links,
		       sizeof(msm8996_tasha_be_dai_links));

		dailink = msm8996_tasha_dai_links;
		len_4 = len_3 + ARRAY_SIZE(msm8996_tasha_be_dai_links);
	}

	if (of_property_read_bool(dev->of_node, "qcom,hdmi-audio-rx")) {
		dev_dbg(dev, "%s(): hdmi audio support present\n",
				__func__);
		memcpy(dailink + len_4, msm8996_hdmi_dai_link,
			sizeof(msm8996_hdmi_dai_link));
		len_4 += ARRAY_SIZE(msm8996_hdmi_dai_link);
	} else {
		dev_dbg(dev, "%s(): No hdmi audio support\n", __func__);
	}

	if (card) {
		card->dai_link = dailink;
		card->num_links = len_4;
	}

	return card;
}

#ifdef USE_WSA_AMP
static int msm8996_init_wsa_dev(struct platform_device *pdev,
				struct snd_soc_card *card)
{
	struct device_node *wsa_of_node;
	u32 wsa_max_devs;
	u32 wsa_dev_cnt;
	char *dev_name_str = NULL;
	struct msm8996_wsa881x_dev_info *wsa881x_dev_info;
	const char *wsa_auxdev_name_prefix[1];
	int found = 0;
	int i;
	int ret;

	
	ret = of_property_read_u32(pdev->dev.of_node,
				   "qcom,wsa-max-devs", &wsa_max_devs);
	if (ret) {
		dev_dbg(&pdev->dev,
			 "%s: wsa-max-devs property missing in DT %s, ret = %d\n",
			 __func__, pdev->dev.of_node->full_name, ret);
		return 0;
	}
	if (wsa_max_devs == 0) {
		dev_warn(&pdev->dev,
			 "%s: Max WSA devices is 0 for this target?\n",
			 __func__);
		return 0;
	}

	
	wsa_dev_cnt = of_count_phandle_with_args(pdev->dev.of_node,
						 "qcom,wsa-devs", NULL);
	if (wsa_dev_cnt == -ENOENT) {
		dev_warn(&pdev->dev, "%s: No wsa device defined in DT.\n",
			 __func__);
		return 0;
	} else if (wsa_dev_cnt <= 0) {
		dev_err(&pdev->dev,
			"%s: Error reading wsa device from DT. wsa_dev_cnt = %d\n",
			__func__, wsa_dev_cnt);
		return -EINVAL;
	}

	if (wsa_dev_cnt < wsa_max_devs) {
		dev_dbg(&pdev->dev,
			"%s: wsa_max_devs = %d cannot exceed wsa_dev_cnt = %d\n",
			__func__, wsa_max_devs, wsa_dev_cnt);
		wsa_max_devs = wsa_dev_cnt;
	}

	
	ret = of_property_count_strings(pdev->dev.of_node,
					"qcom,wsa-aux-dev-prefix");
	if (ret != wsa_dev_cnt) {
		dev_err(&pdev->dev,
			"%s: expecting %d wsa prefix. Defined only %d in DT\n",
			__func__, wsa_dev_cnt, ret);
		return -EINVAL;
	}

	wsa881x_dev_info = devm_kcalloc(&pdev->dev, wsa_max_devs,
					sizeof(struct msm8996_wsa881x_dev_info),
					GFP_KERNEL);
	if (!wsa881x_dev_info)
		return -ENOMEM;

	for (i = 0; i < wsa_dev_cnt; i++) {
		wsa_of_node = of_parse_phandle(pdev->dev.of_node,
					    "qcom,wsa-devs", i);
		if (unlikely(!wsa_of_node)) {
			
			dev_err(&pdev->dev,
				"%s: wsa dev node is not present\n",
				__func__);
			return -EINVAL;
		}
		if (soc_find_component(wsa_of_node, NULL)) {
			
			wsa881x_dev_info[found].of_node = wsa_of_node;
			wsa881x_dev_info[found].index = i;
			found++;
			if (found == wsa_max_devs)
				break;
		}
	}

	if (found < wsa_max_devs) {
		dev_dbg(&pdev->dev,
			"%s: failed to find %d components. Found only %d\n",
			__func__, wsa_max_devs, found);
		return -EPROBE_DEFER;
	}
	dev_info(&pdev->dev,
		"%s: found %d wsa881x devices registered with ALSA core\n",
		__func__, found);

	card->num_aux_devs = wsa_max_devs;
	card->num_configs = wsa_max_devs;

	
	msm8996_aux_dev = devm_kcalloc(&pdev->dev, card->num_aux_devs,
				       sizeof(struct snd_soc_aux_dev),
				       GFP_KERNEL);
	if (!msm8996_aux_dev)
		return -ENOMEM;

	
	msm8996_codec_conf = devm_kcalloc(&pdev->dev, card->num_aux_devs,
					  sizeof(struct snd_soc_codec_conf),
					  GFP_KERNEL);
	if (!msm8996_codec_conf)
		return -ENOMEM;

	for (i = 0; i < card->num_aux_devs; i++) {
		dev_name_str = devm_kzalloc(&pdev->dev, DEV_NAME_STR_LEN,
					    GFP_KERNEL);
		if (!dev_name_str)
			return -ENOMEM;

		ret = of_property_read_string_index(pdev->dev.of_node,
						    "qcom,wsa-aux-dev-prefix",
						    wsa881x_dev_info[i].index,
						    wsa_auxdev_name_prefix);
		if (ret) {
			dev_err(&pdev->dev,
				"%s: failed to read wsa aux dev prefix, ret = %d\n",
				__func__, ret);
			return -EINVAL;
		}

		snprintf(dev_name_str, strlen("wsa881x.%d"), "wsa881x.%d", i);
		msm8996_aux_dev[i].name = dev_name_str;
		msm8996_aux_dev[i].codec_name = NULL;
		msm8996_aux_dev[i].codec_of_node =
					wsa881x_dev_info[i].of_node;
		msm8996_aux_dev[i].init = msm8996_wsa881x_init;
		msm8996_codec_conf[i].dev_name = NULL;
		msm8996_codec_conf[i].name_prefix = wsa_auxdev_name_prefix[0];
		msm8996_codec_conf[i].of_node =
					wsa881x_dev_info[i].of_node;
	}
	card->codec_conf = msm8996_codec_conf;
	card->aux_dev = msm8996_aux_dev;

	return 0;
}
#endif

static int msm8994_init_ftm_btpcm(struct platform_device *pdev,
				struct aud_btpcm_config *pconfig)
{
	int i = 0;
	if(!pconfig || !pdev) {
		pr_err("%s: %d pdev or pconfig is null\n",__func__,i);
		return -1;
	}

	for(i = 0; i < ARRAY_SIZE(pconfig->gpio); i++) {
		if(!pconfig->gpio[i].gpio_name) {
			pr_err("%s: %d btpcm gpio name is null\n",__func__,i);
			return -1;
		}

		pconfig->gpio[i].gpio_no = of_get_named_gpio(pdev->dev.of_node,
						pconfig->gpio[i].gpio_name, 0);

		if (pconfig->gpio[i].gpio_no < 0) {
			pr_err( "property %s not detected in node\n", pconfig->gpio[i].gpio_name);
			return -1;
		} else {
			pr_info("%s: btpcm gpio %s no %d\n",__func__, pconfig->gpio[i].gpio_name, pconfig->gpio[i].gpio_no);
		}
	}
	pconfig->init = 1;
	return 0;
}

static void htc_card_det(struct work_struct *work)
{
	pr_err("%s: Trigger BUG due to sound card not register in %d ms \n", __func__, CARD_TIMEOUT);
	BUG();
}

#ifdef CONFIG_USE_AS_HS
static int headset_dt_parser(struct platform_device *pdev, struct wcd_mbhc_config *wcd_mbhc_cfg){
	int i = 0;
	int id_gpio_count = 0;
	int switch_gpio_count = 0;
	uint32_t min_max_array[2]; 
	const char *id_gpio_name = "htc,aud_gpio_ids";
	const char *switch_gpio_name = "htc,aud_gpio_switches";
	const char *hsmic_bias_string = "htc,hsmic_2v85_en";
	const char *adapter_35mm_string = "htc,adapter_35mm_threshold";
	const char *adapter_25mm_string = "htc,adapter_25mm_threshold";
	const char *adc_channel_string = "htc,headset_adc_channel";
	const char *name;
	int ret = 0;

	pr_debug("%s: start parser\n", __func__);

	id_gpio_count = of_property_count_strings(pdev->dev.of_node, id_gpio_name);
	switch_gpio_count = of_property_count_strings(pdev->dev.of_node, switch_gpio_name);
	if (IS_ERR_VALUE(id_gpio_count) && IS_ERR_VALUE(switch_gpio_count)) {
		pr_err("%s: Failed to get %s = %d, %s = %d\n", __func__,
			id_gpio_name, id_gpio_count,
			switch_gpio_name, switch_gpio_count);
		return -EINVAL;
	}

	if (id_gpio_count != TYPEC_ID_MAX) {
		id_gpio_count = TYPEC_ID_MAX;
		pr_err("%s: id_gpio_count incorrect, please check dts config", __func__);
		return -EINVAL;
	}

	if (switch_gpio_count != HEADSET_SWITCH_MAX) {
		switch_gpio_count = TYPEC_ID_MAX;
		pr_err("%s: switch_gpio_count incorrect, please check dts config", __func__);
		return -EINVAL;
	}

	for (i = 0; i < id_gpio_count; i++){
		ret = of_property_read_string_index(pdev->dev.of_node, id_gpio_name, i, &name);
		if (ret) {
			pr_err("%s: of read string %s index %d error %d\n",
				__func__, id_gpio_name, i, ret);
			return ret;
		}

		wcd_mbhc_cfg->htc_headset_cfg.id_gpio[i] = of_get_named_gpio(pdev->dev.of_node,
			name, 0);
		if (gpio_is_valid(wcd_mbhc_cfg->htc_headset_cfg.id_gpio[i])) {
			pr_info("%s: gpio %s parse success gpio no %d\n", __func__,
				name, wcd_mbhc_cfg->htc_headset_cfg.id_gpio[i]);
			ret = gpio_request_one(wcd_mbhc_cfg->htc_headset_cfg.id_gpio[i],
				GPIOF_DIR_IN, name);
			if (ret) {
				pr_err("%s: gpio %s request failed with err %d\n", __func__,
					name, ret);
				return ret;
			}
		} else {
			pr_err("%s: gpio %s parse fail\n", __func__, name);
			return -EINVAL;
		}
	}

	for (i = 0; i < switch_gpio_count; i++){
		ret = of_property_read_string_index(pdev->dev.of_node, switch_gpio_name, i, &name);
		if (ret) {
			pr_err("%s: of read string %s index %d error %d\n",
				__func__, switch_gpio_name, i, ret);
			return ret;
		}
		wcd_mbhc_cfg->htc_headset_cfg.switch_gpio[i] = of_get_named_gpio(pdev->dev.of_node,
			name, 0);
		if (gpio_is_valid(wcd_mbhc_cfg->htc_headset_cfg.switch_gpio[i])) {
			pr_info("%s: gpio %s parse success gpio no %d\n", __func__,
				name, wcd_mbhc_cfg->htc_headset_cfg.switch_gpio[i]);
			if (i == 1) { 
				ret = gpio_request_one(wcd_mbhc_cfg->htc_headset_cfg.switch_gpio[i],
					GPIOF_OUT_INIT_HIGH, name);
			} else {
				ret = gpio_request_one(wcd_mbhc_cfg->htc_headset_cfg.switch_gpio[i],
					GPIOF_OUT_INIT_LOW, name);
			}
			if (ret) {
				pr_err("%s: gpio %s request failed with err %d\n", __func__,
					name, ret);
				return ret;
			}
		} else {
			pr_err("%s: gpio %s parse fail\n", __func__, name);
			return -EINVAL;
		}
	}

	wcd_mbhc_cfg->htc_headset_cfg.ext_micbias = of_get_named_gpio(pdev->dev.of_node,
		hsmic_bias_string, 0);
	if (gpio_is_valid(wcd_mbhc_cfg->htc_headset_cfg.ext_micbias)) {
		pr_info("%s: gpio %s parse success gpio no %d\n", __func__,
			hsmic_bias_string,
			wcd_mbhc_cfg->htc_headset_cfg.ext_micbias);
		ret = gpio_request_one(wcd_mbhc_cfg->htc_headset_cfg.ext_micbias,
			GPIOF_OUT_INIT_LOW, hsmic_bias_string);
		if (ret) {
			pr_err("%s: gpio %s request failed with err %d\n", __func__,
				hsmic_bias_string, ret);
			return ret;
		}
	} else {
		pr_err("%s: gpio %s parse fail\n", __func__,
			hsmic_bias_string);
		return -EINVAL;
	}

	ret = of_property_read_u32_array(pdev->dev.of_node,
			adapter_35mm_string,
			min_max_array, 2);

	if (ret < 0) {
			pr_err("%s: adapter_35mm_string parser err\n", __func__);
		} else {
			wcd_mbhc_cfg->htc_headset_cfg.adc_35mm_min = min_max_array[0];
			wcd_mbhc_cfg->htc_headset_cfg.adc_35mm_max = min_max_array[1];
			pr_info("%s: adapter_35mm_string  min: = %d max: = %d\n", __func__, wcd_mbhc_cfg->htc_headset_cfg.adc_35mm_min , wcd_mbhc_cfg->htc_headset_cfg.adc_35mm_max);
		}


	ret = of_property_read_u32_array(pdev->dev.of_node,
			adapter_25mm_string,
			min_max_array, 2);

	if (ret < 0) {
			pr_err("%s: adapter_25mm_string parser err\n", __func__);
		} else {
			wcd_mbhc_cfg->htc_headset_cfg.adc_25mm_min = min_max_array[0];
			wcd_mbhc_cfg->htc_headset_cfg.adc_25mm_max = min_max_array[1];
			pr_info("%s: adapter_25mm_string  min: = %d max: = %d\n", __func__, wcd_mbhc_cfg->htc_headset_cfg.adc_25mm_min , wcd_mbhc_cfg->htc_headset_cfg.adc_25mm_max);
		}

	ret = of_property_read_u32(pdev->dev.of_node, adc_channel_string, &wcd_mbhc_cfg->htc_headset_cfg.adc_channel);
	if (ret < 0) {
		pr_err("%s: adc channel parser err\n", __func__);
	} else
		pr_info("%s: adc_channel = %d\n", __func__, wcd_mbhc_cfg->htc_headset_cfg.adc_channel);

	wcd_mbhc_cfg->htc_headset_cfg.get_adc_value = hs_qpnp_remote_adc;
	wcd_mbhc_cfg->htc_headset_cfg.htc_headset_init = true;

	pr_debug("%s: parse end\n", __func__);
	return 0;
}

static int msm8996_setparam(htc_adsp_params_ioctl_t *ctrl)
{
	enum HTC_ADM_EFFECT_ID effect_id = HTC_ADM_EFFECT_MAX;
	int ret = 0;
	int module = *(int *)(ctrl->params);
	int param_id = *((int *)(ctrl->params) + 1);
	int data_size  = *((int *)(ctrl->params) + 2);
	int data  = *((int *)(ctrl->params) + 3);
	int type = ctrl->type;
	int total_size = ctrl->size;

	switch (type) {
	case eEF_as2_coef_left_im:
		effect_id = HTC_ADM_EFFECT_AS_DATA1;
		break;
	case eEF_as2_coef_left_re:
		effect_id = HTC_ADM_EFFECT_AS_DATA2;
		break;
	case eEF_as2_coef_right_im:
		effect_id = HTC_ADM_EFFECT_AS_DATA3;
		break;
	case eEF_as2_coef_right_re:
		effect_id = HTC_ADM_EFFECT_AS_DATA4;
		break;
	case eEF_lmt_coef:
		effect_id = HTC_ADM_EFFECT_AS_DATA5;
		break;
	case eEF_lmt_rtc:
		effect_id = HTC_ADM_EFFECT_AS_DATA6;
		break;
	case eEF_as2_rtc:
		effect_id = HTC_ADM_EFFECT_AS_DATA7;
		break;
	default:
		effect_id = HTC_ADM_EFFECT_MAX;
		pr_err("%s error type %d", __func__, type);
		break;
	}

	pr_info("AdaptSound DSP Packet type %d total size %d ",
		type, total_size);
	pr_info("module 0x%x param id 0x%x data_size %d data 0x%x\n",
			 module, param_id, data_size, data);

	mutex_lock(&htc_adaptivesound_enable_mutex);
	ret = htc_adm_effect_control(effect_id,
					AFE_PORT_ID_SLIMBUS_MULTI_CHAN_0_RX,
					AFE_COPP_ID_ADAPTIVE_AUDIO_20,
					total_size,
					(void *)ctrl->params);
	if (ret < 0)
		pr_err("%s: ret %d error\n", __func__, ret);

	mutex_unlock(&htc_adaptivesound_enable_mutex);

	return 0;
}

static int msm8996_get_headsetType(void)
{
	int ret = 0;
	struct snd_soc_codec *codec;

	list_for_each_entry(codec,
			&snd_soc_card_tasha_msm8996.codec_dev_list, card_list) {
		pr_debug("%s: sound card codec name %s\n",
				__func__, dev_name(codec->dev));
		if (!strncmp(dev_name(codec->dev),
			"tasha_codec", sizeof("tasha_codec")))
			return tasha_get_headsetType(codec);
	}

	return ret;

}
#endif

int msm8994_enable_24b_audio(void)
{
	return 1;
}

static int hw_compon;
static int htc_msm8996_get_hw_component(void)
{
	return hw_compon;
}

static struct acoustic_ops acoustic = {
	.get_hw_component = htc_msm8996_get_hw_component,
	.enable_24b_audio = msm8994_enable_24b_audio,
#ifdef CONFIG_USE_AS_HS
	.msm_setparam = msm8996_setparam,
	.get_headsetType = msm8996_get_headsetType,
#endif
};

static int msm8996_asoc_machine_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card;
	struct msm8996_asoc_mach_data *pdata;
	const char *mbhc_audio_jack_type = NULL;
	char *mclk_freq_prop_name;
	const struct of_device_id *match;
	int ret;
	int tfa9888_dev = 0, rt5503_dev = 0; 

	if((apr_get_q6_state() == APR_SUBSYS_LOADED) && card_reg == -1) {
		pr_info("%s: schedule_delayed_work with card_det_work\n", __func__);
		schedule_delayed_work(&card_det_work, msecs_to_jiffies(CARD_TIMEOUT));
		card_reg = 0;
	}

	if (!pdev->dev.of_node) {
		dev_err(&pdev->dev, "No platform supplied from device tree\n");
		return -EINVAL;
	}

	pdata = devm_kzalloc(&pdev->dev,
			sizeof(struct msm8996_asoc_mach_data), GFP_KERNEL);
	if (!pdata) {
		dev_err(&pdev->dev, "Can't allocate msm8996_asoc_mach_data\n");
		return -ENOMEM;
	}

	card = populate_snd_card_dailinks(&pdev->dev);
	if (!card) {
		dev_err(&pdev->dev, "%s: Card uninitialized\n", __func__);
		ret = -EINVAL;
		goto err;
	}
	card->dev = &pdev->dev;
	platform_set_drvdata(pdev, card);
	snd_soc_card_set_drvdata(card, pdata);

	atomic_set(&msm_pri_mi2s_data.mi2s_rsc_ref, 0);
	atomic_set(&msm_sec_mi2s_data.mi2s_rsc_ref, 0);
	atomic_set(&msm_tert_mi2s_data.mi2s_rsc_ref, 0);
	atomic_set(&msm_quat_mi2s_data.mi2s_rsc_ref, 0);

	ret = snd_soc_of_parse_card_name(card, "qcom,model");
	if (ret) {
		dev_err(&pdev->dev, "parse card name failed, err:%d\n",
			ret);
		goto err;
	}

	ret = snd_soc_of_parse_audio_routing(card, "qcom,audio-routing");
	if (ret) {
		dev_err(&pdev->dev, "parse audio routing failed, err:%d\n",
			ret);
		goto err;
	}

	match = of_match_node(msm8996_asoc_machine_of_match,
			pdev->dev.of_node);
	if (!match) {
		dev_err(&pdev->dev, "%s: no matched codec is found.\n",
			__func__);
		goto err;
	}

	mclk_freq_prop_name = "qcom,tasha-mclk-clk-freq";

	ret = of_property_read_u32(pdev->dev.of_node,
			mclk_freq_prop_name, &pdata->mclk_freq);
	if (ret) {
		dev_err(&pdev->dev,
			"Looking up %s property in node %s failed, err%d\n",
			mclk_freq_prop_name,
			pdev->dev.of_node->full_name, ret);
		goto err;
	}

	if (pdata->mclk_freq != CODEC_EXT_CLK_RATE) {
		dev_err(&pdev->dev, "unsupported mclk freq %u\n",
			pdata->mclk_freq);
		ret = -EINVAL;
		goto err;
	}

	spdev = pdev;

#ifdef CONFIG_RT_REGMAP
	pdata->audio_1v8_hph_en_gpio = of_get_named_gpio(pdev->dev.of_node,
				"qcom,audio-1v8-hph-en-gpio", 0);
	if (pdata->audio_1v8_hph_en_gpio < 0) {
		dev_info(&pdev->dev, "property %s not detected in node %s\n",
			"qcom,audio-1v8-hph-en-gpio",
			pdev->dev.of_node->full_name);
	} else {
		dev_info(&pdev->dev, "%s detected %d\n",
			"qcom,audio-1v8-hph-en-gpio", pdata->audio_1v8_hph_en_gpio);
		ret = gpio_request(pdata->audio_1v8_hph_en_gpio, "audio_1v8_hph_en_gpio");
		if (ret < 0) {
			pr_err("%s: audio-1v8-hph-en gpio request %d error %d\n",__func__, pdata->audio_1v8_hph_en_gpio, ret);
		} else {
			ret = gpio_direction_output(pdata->audio_1v8_hph_en_gpio, 1);
			pr_info("%s: audio-1v8-hph-en output high\n", __func__);
		}
	}
	pdata->rt5503_reset_gpio = of_get_named_gpio(pdev->dev.of_node,
				"qcom,rt5503-reset-gpio", 0);
	if (pdata->rt5503_reset_gpio < 0) {
		dev_info(&pdev->dev, "property %s not detected in node %s\n",
			"qcom,rt5503-reset-gpio",
			pdev->dev.of_node->full_name);
	} else {
		dev_info(&pdev->dev, "%s detected %d\n",
			"qcom,rt5503-reset-gpio", pdata->rt5503_reset_gpio);
		ret = gpio_request(pdata->rt5503_reset_gpio, "rt5503_reset_gpio");
		if (ret < 0) {
			pr_err("%s: rt5503_reset_gpio gpio request %d error %d\n",__func__, pdata->rt5503_reset_gpio, ret);
		} else {
			ret = gpio_direction_output(pdata->rt5503_reset_gpio, 1);
			pr_info("%s: rt5503_reset_gpio output high\n", __func__);
		}
	}
#endif
	ret = msm8994_init_ftm_btpcm(pdev, &htc_aud_btpcm_config);
	if (ret < 0) {
		pr_warn("%s: init ftm btpcm failed with %d (Non-issue for non-BRCM BT chip.)", __func__, ret);
	}

#ifdef CONFIG_USE_AS_HS
	if (!wcd_mbhc_cfg.htc_headset_cfg.htc_headset_init) {
		headset_dt_parser(pdev, &wcd_mbhc_cfg);
	}
#endif

	ret = msm8996_populate_dai_link_component_of_node(card);
	if (ret) {
		ret = -EPROBE_DEFER;
		goto err;
	}
#ifdef USE_WSA_AMP
	ret = msm8996_init_wsa_dev(pdev, card);
	if (ret)
		goto err;
#endif

	pdata->hph_en1_gpio = of_get_named_gpio(pdev->dev.of_node,
						"qcom,hph-en1-gpio", 0);
	if (pdata->hph_en1_gpio < 0) {
		dev_dbg(&pdev->dev, "%s: %s property not found %d\n",
			__func__, "qcom,hph-en1-gpio", pdata->hph_en1_gpio);
	}

	pdata->hph_en0_gpio = of_get_named_gpio(pdev->dev.of_node,
						"qcom,hph-en0-gpio", 0);
	if (pdata->hph_en0_gpio < 0) {
		dev_dbg(&pdev->dev, "%s: %s property not found %d\n",
			__func__, "qcom,hph-en0-gpio", pdata->hph_en0_gpio);
	}
	ret = msm8996_prepare_hifi(card);
	if (ret)
		dev_dbg(&pdev->dev, "msm8996_prepare_hifi failed (%d)\n",
			ret);

	ret = snd_soc_register_card(card);
	if (ret == -EPROBE_DEFER) {
		if (codec_reg_done)
			ret = -EINVAL;
		goto err;
	} else if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n",
			ret);
		goto err;
	}
	dev_info(&pdev->dev, "Sound card %s registered\n", card->name);

	pr_info("%s:  cancel_delayed_work with card_det_work\n", __func__);
	cancel_delayed_work_sync(&card_det_work);

	ret = of_property_read_string(pdev->dev.of_node,
		"qcom,mbhc-audio-jack-type", &mbhc_audio_jack_type);
	if (ret) {
		dev_dbg(&pdev->dev, "Looking up %s property in node %s failed",
			"qcom,mbhc-audio-jack-type",
			pdev->dev.of_node->full_name);
		dev_dbg(&pdev->dev, "Jack type properties set to default");
	} else {
		if (!strcmp(mbhc_audio_jack_type, "4-pole-jack")) {
			dev_dbg(&pdev->dev, "This hardware has 4 pole jack");
		} else if (!strcmp(mbhc_audio_jack_type, "5-pole-jack")) {
			dev_dbg(&pdev->dev, "This hardware has 5 pole jack");
		} else if (!strcmp(mbhc_audio_jack_type, "6-pole-jack")) {
			dev_dbg(&pdev->dev, "This hardware has 6 pole jack");
		} else {
			dev_dbg(&pdev->dev, "Unknown value, set to default");
		}
	}
	pdata->us_euro_gpio = of_get_named_gpio(pdev->dev.of_node,
				"qcom,us-euro-gpios", 0);
	if (pdata->us_euro_gpio < 0) {
		dev_info(&pdev->dev, "property %s not detected in node %s",
			"qcom,us-euro-gpios",
			pdev->dev.of_node->full_name);
	} else {
		dev_dbg(&pdev->dev, "%s detected %d",
			"qcom,us-euro-gpios", pdata->us_euro_gpio);
#ifdef CONFIG_USE_CODEC_MBHC
		wcd_mbhc_cfg.swap_gnd_mic = msm8996_swap_gnd_mic;
#endif
	}

	ret = msm8996_prepare_us_euro(card);
	if (ret)
		dev_info(&pdev->dev, "msm8996_prepare_us_euro failed (%d)\n",
			ret);

	mutex_init(&htc_adaptivesound_enable_mutex);

	ret = of_property_read_u32(pdev->dev.of_node, "htc-tfa9888", &tfa9888_dev);
	if (ret) {
		pr_info("%s: Missing htc-tfa9888 in dt node\n", __func__);
		tfa9888_dev = 0;
	} else {
		pr_info("%s: htc-tfa9888 is %s in dt node\n", __func__, tfa9888_dev ? "true" : "false");
		if (tfa9888_dev) {
			hw_compon |= HTC_AUDIO_TFA9888;
		}
	}
	of_property_read_u32(pdev->dev.of_node, "htc-rt5503", &rt5503_dev);
	if (ret) {
		pr_info("%s: Missing htc-rt5503 in dt node\n", __func__);
		rt5503_dev = 0;
	} else {
		pr_info("%s: htc-rt5503 is %s in dt node\n", __func__, rt5503_dev ? "true" : "false");
		if (rt5503_dev) {
			hw_compon |= HTC_AUDIO_RT5503;
		}
	}
	htc_acoustic_register_ops(&acoustic);
	pr_info("%s: probe successfully\n", __func__);

	return 0;
err:
	if (pdata->us_euro_gpio > 0) {
		dev_dbg(&pdev->dev, "%s free us_euro gpio %d\n",
			__func__, pdata->us_euro_gpio);
		gpio_free(pdata->us_euro_gpio);
		pdata->us_euro_gpio = 0;
	}
	if (pdata->hph_en1_gpio > 0) {
		dev_dbg(&pdev->dev, "%s free hph_en1_gpio %d\n",
			__func__, pdata->hph_en1_gpio);
		gpio_free(pdata->hph_en1_gpio);
		pdata->hph_en1_gpio = 0;
	}
	if (pdata->hph_en0_gpio > 0) {
		dev_dbg(&pdev->dev, "%s free hph_en0_gpio %d\n",
			__func__, pdata->hph_en0_gpio);
		gpio_free(pdata->hph_en0_gpio);
		pdata->hph_en0_gpio = 0;
	}
#ifdef CONFIG_RT_REGMAP
	if (pdata->audio_1v8_hph_en_gpio > 0) {
		gpio_free(pdata->audio_1v8_hph_en_gpio);
		pdata->audio_1v8_hph_en_gpio = 0;
	}
	if (pdata->rt5503_reset_gpio > 0) {
		gpio_free(pdata->rt5503_reset_gpio);
		pdata->rt5503_reset_gpio = 0;
	}
#endif
	devm_kfree(&pdev->dev, pdata);
	return ret;
}

static int msm8996_asoc_machine_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct msm8996_asoc_mach_data *pdata =
				snd_soc_card_get_drvdata(card);

	mutex_destroy(&htc_adaptivesound_enable_mutex);

	if (gpio_is_valid(ext_us_amp_gpio))
		gpio_free(ext_us_amp_gpio);

	gpio_free(pdata->us_euro_gpio);
	gpio_free(pdata->hph_en1_gpio);
	gpio_free(pdata->hph_en0_gpio);

	if (msm8996_liquid_dock_dev != NULL) {
		switch_dev_unregister(&msm8996_liquid_dock_dev->audio_sdev);

		if (msm8996_liquid_dock_dev->dock_plug_irq)
			free_irq(msm8996_liquid_dock_dev->dock_plug_irq,
				 msm8996_liquid_dock_dev);

		if (msm8996_liquid_dock_dev->dock_plug_gpio)
			gpio_free(msm8996_liquid_dock_dev->dock_plug_gpio);

		kfree(msm8996_liquid_dock_dev);
		msm8996_liquid_dock_dev = NULL;
	}
	snd_soc_unregister_card(card);

	return 0;
}

static struct platform_driver msm8996_asoc_machine_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
		.of_match_table = msm8996_asoc_machine_of_match,
	},
	.probe = msm8996_asoc_machine_probe,
	.remove = msm8996_asoc_machine_remove,
};
module_platform_driver(msm8996_asoc_machine_driver);

MODULE_DESCRIPTION("ALSA SoC msm");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
MODULE_DEVICE_TABLE(of, msm8996_asoc_machine_of_match);
