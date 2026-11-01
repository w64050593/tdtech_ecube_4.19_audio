/*
 * abi_probe.c - 运行内核 ALSA 结构体 ABI 探针
 *
 * 用途：把编译期（本模块头文件）算出的结构体大小/偏移打印出来，
 *       同时用运行内核真正提供的 snd_card_new()/snd_pcm_new() 创建
 *       真实对象，验证编译期布局与运行内核是否一致。
 *
 * 关键点：snd_card_new() 的 parent 不能传 NULL。
 *   vendor 内核的 snd_card_new() 在 snprintf(card->irq_descr, ...) 里会调用
 *   dev_driver_string(card->dev)，parent 为 NULL 时会直接空指针解引用
 *   （表现为 SP/PC alignment exception 或 Data abort）。这里传 &platform_bus。
 *
 * 编译：
 *   make -C <内核树> M=<本目录> ARCH=arm64 \
 *        CROSS_COMPILE=aarch64-linux-gnu- CC=aarch64-linux-gnu-gcc modules
 * 运行：
 *   insmod abi_probe.ko
 *   dmesg | grep ABI
 *   rmmod abi_probe
 */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>

static int __init abi_probe_init(void)
{
	struct snd_card *card = NULL;
	struct snd_pcm *pcm = NULL;
	struct snd_pcm_str *pstr;
	struct snd_pcm_substream *subs;
	int err;

	pr_err("ABI: sizeof(pm_qos_request)=%zu sizeof(rw_semaphore)=%zu\n",
	       sizeof(struct pm_qos_request), sizeof(struct rw_semaphore));
	pr_err("ABI: sizeof(snd_card)=%zu ctl_dev=%zu dev=%zu card_dev=%zu "
	       "dev_groups=%zu registered=%zu remove_sleep=%zu offline=%zu\n",
	       sizeof(struct snd_card),
	       offsetof(struct snd_card, ctl_dev),
	       offsetof(struct snd_card, dev),
	       offsetof(struct snd_card, card_dev),
	       offsetof(struct snd_card, dev_groups),
	       offsetof(struct snd_card, registered),
	       offsetof(struct snd_card, remove_sleep),
	       offsetof(struct snd_card, offline));
	pr_err("ABI: sizeof(snd_pcm)=%zu streams=%zu private_data=%zu "
	       "private_free=%zu\n",
	       sizeof(struct snd_pcm), offsetof(struct snd_pcm, streams),
	       offsetof(struct snd_pcm, private_data),
	       offsetof(struct snd_pcm, private_free));
	pr_err("ABI: sizeof(snd_pcm_str)=%zu dev=%zu substream=%zu\n",
	       sizeof(struct snd_pcm_str), offsetof(struct snd_pcm_str, dev),
	       offsetof(struct snd_pcm_str, substream));
	pr_err("ABI: sizeof(snd_pcm_substream)=%zu pm_qos=%zu ops=%zu "
	       "runtime=%zu runtime_lock=%zu timer=%zu wait_time=%zu "
	       "link_list=%zu self_group=%zu group=%zu\n",
	       sizeof(struct snd_pcm_substream),
	       offsetof(struct snd_pcm_substream, latency_pm_qos_req),
	       offsetof(struct snd_pcm_substream, ops),
	       offsetof(struct snd_pcm_substream, runtime),
	       offsetof(struct snd_pcm_substream, runtime_lock),
	       offsetof(struct snd_pcm_substream, timer),
	       offsetof(struct snd_pcm_substream, wait_time),
	       offsetof(struct snd_pcm_substream, link_list),
	       offsetof(struct snd_pcm_substream, self_group),
	       offsetof(struct snd_pcm_substream, group));
	pr_err("ABI: sizeof(snd_pcm_group)=%zu lock=%zu mutex=%zu "
	       "substreams=%zu count=%zu\n",
	       sizeof(struct snd_pcm_group),
	       offsetof(struct snd_pcm_group, lock),
	       offsetof(struct snd_pcm_group, mutex),
	       offsetof(struct snd_pcm_group, substreams),
	       offsetof(struct snd_pcm_group, count));

	/* parent 必须是有效的 struct device，不能用 NULL */
	err = snd_card_new(&platform_bus, -1, "abiprobe", THIS_MODULE, 0, &card);
	if (err < 0) {
		pr_err("ABI: snd_card_new failed: %d\n", err);
		return 0;
	}

	err = snd_pcm_new(card, "abipcm", 0, 1, 1, &pcm);
	if (err < 0) {
		pr_err("ABI: snd_pcm_new failed: %d\n", err);
		snd_card_free(card);
		return 0;
	}

	pr_err("ABI: runtime card=%px ksize=%zu pcm=%px\n",
	       card, ksize(card), pcm);

	pstr = &pcm->streams[SNDRV_PCM_STREAM_PLAYBACK];
	subs = pstr->substream;
	pr_err("ABI: runtime pstr=%px subs=%px\n", pstr, subs);
	if (subs) {
		pr_err("ABI: runtime subs->pcm=%px (expect %px) %s\n",
		       subs->pcm, pcm, subs->pcm == pcm ? "MATCH" : "MISMATCH");
		pr_err("ABI: runtime subs->pstr=%px (expect %px) %s\n",
		       subs->pstr, pstr, subs->pstr == pstr ? "MATCH" : "MISMATCH");
		pr_err("ABI: runtime subs->name='%s' number=%d stream=%d\n",
		       subs->name, subs->number, subs->stream);
	}
	pr_err("ABI: runtime pcm->private_data=%px private_free=%px\n",
	       pcm->private_data, pcm->private_free);

	snd_card_free(card);
	pr_err("ABI: probe done\n");
	return 0;
}

static void __exit abi_probe_exit(void) {}

module_init(abi_probe_init);
module_exit(abi_probe_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ALSA ABI probe for Rockchip vendor kernel 4.19");
