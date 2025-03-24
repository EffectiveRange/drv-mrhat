// SPDX-License-Identifier: GPL-2.0
// MrHat platform driver
// Copyright (C) 2024 Ferenc Janky & Attila Gombos

#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/timer.h>
#include <linux/workqueue.h>

struct mrhat_device {
  struct device *dev;
  struct timer_list hb_timer;
  struct workqueue_struct *wq;
  struct work_struct hb_work;
  int hb_level;
  struct gpio_desc *gpio_hb;
  u32 period_ms;
  u32 pulse_width_ms;
  u32 pulse_to_period_ms;
};

static void mrhat_hb_work_func(struct work_struct *work) {
  unsigned long jiffies_start = jiffies;
  unsigned long jiffies_end;
  struct mrhat_device *mrhat_dev =
      container_of(work, struct mrhat_device, hb_work);
  int curr_level = mrhat_dev->hb_level;
  mrhat_dev->hb_level = mrhat_dev->hb_level ? 0 : 1;
  gpiod_set_value_cansleep(mrhat_dev->gpio_hb, mrhat_dev->hb_level);
  jiffies_end = jiffies;
  mod_timer(&mrhat_dev->hb_timer,
            jiffies +
                (curr_level ? msecs_to_jiffies(mrhat_dev->pulse_to_period_ms)
                            : msecs_to_jiffies(mrhat_dev->pulse_width_ms)) -
                (jiffies_end - jiffies_start));
}

static void timer_callback(struct timer_list *t) {
  struct mrhat_device *mrhat_dev =
      container_of(t, struct mrhat_device, hb_timer);
  queue_work(mrhat_dev->wq, &mrhat_dev->hb_work);
}

static const struct of_device_id mrhat_dt_ids[] = {{
                                                       .compatible = "er,mrhat",
                                                   },
                                                   {}};
MODULE_DEVICE_TABLE(of, mrhat_dt_ids);

static void mrhat_cleanup_workqueue(void *data) {
  struct mrhat_device *mrhat_dev = data;
  dev_info(mrhat_dev->dev, "cleaning up workqueue");
  destroy_workqueue(mrhat_dev->wq);
}

static void mrhat_cleanup_work(void *data) {
  struct work_struct *work = data;
  struct mrhat_device *mrhat_dev =
      container_of(work, struct mrhat_device, hb_work);
  dev_info(mrhat_dev->dev, "cleaning up work struct");
  cancel_work_sync(work);
}

static void mrhat_cleanup_timer(void *data) {
  struct timer_list *timer = data;
  struct mrhat_device *mrhat_dev =
      container_of(timer, struct mrhat_device, hb_timer);
  dev_info(mrhat_dev->dev, "cleaning up timer");
  del_timer_sync(timer);
}

static int er_mrhat_probe(struct platform_device *pdev) {
  int ret;

  struct mrhat_device *mrhat_dev =
      devm_kzalloc(&pdev->dev, sizeof(struct mrhat_device), GFP_KERNEL);
  if (IS_ERR(mrhat_dev)) {
    dev_err(&pdev->dev, "failed to allocate memory for device struct");
    return PTR_ERR(mrhat_dev);
  }
  mrhat_dev->dev = &pdev->dev;
  platform_set_drvdata(pdev, mrhat_dev);

  mrhat_dev->gpio_hb =
      devm_gpiod_get(mrhat_dev->dev, "heartbeat", GPIOD_OUT_HIGH);
  if (IS_ERR(mrhat_dev->gpio_hb)) {
    ret = PTR_ERR(mrhat_dev->gpio_hb);
    dev_err(mrhat_dev->dev, "failed to request GPIO pin:%d", ret);
    return ret;
  }
  mrhat_dev->hb_level = 1;

  mrhat_dev->wq = create_singlethread_workqueue("hb_wq");
  if (IS_ERR(mrhat_dev->wq)) {
    ret = PTR_ERR(mrhat_dev->wq);
    dev_err(mrhat_dev->dev, "failed to create workqueue:%d", ret);
    return ret;
  }

  ret = devm_add_action_or_reset(mrhat_dev->dev, mrhat_cleanup_workqueue,
                                 mrhat_dev);
  if (ret) {
    dev_err(mrhat_dev->dev, "failed to register cleanup action for workqueue");
    return ret;
  }

  INIT_WORK(&mrhat_dev->hb_work, mrhat_hb_work_func);

  ret = devm_add_action_or_reset(mrhat_dev->dev, mrhat_cleanup_work,
                                 &mrhat_dev->hb_work);
  if (ret) {
    dev_err(mrhat_dev->dev,
            "failed to register cleanup action for work thread");
    return ret;
  }

  timer_setup(&mrhat_dev->hb_timer, timer_callback, 0);

  ret = devm_add_action_or_reset(mrhat_dev->dev, mrhat_cleanup_timer,
                                 &mrhat_dev->hb_timer);
  if (ret) {
    dev_err(mrhat_dev->dev, "failed to register cleanup action for timer");
    return ret;
  }

  ret = device_property_read_u32(mrhat_dev->dev, "er,heartbeat-period-ms",
                                 &mrhat_dev->period_ms);
  if (ret) {
    dev_err(mrhat_dev->dev, "failed to read heartbeat period dt property:%d",
            ret);
    return ret;
  }

  ret = device_property_read_u32(mrhat_dev->dev, "er,heartbeat-pulse-width-ms",
                                 &mrhat_dev->pulse_width_ms);
  if (ret) {
    dev_err(mrhat_dev->dev,
            "failed to read heartbeat pulse width dt property:%d", ret);
    return ret;
  }

  mrhat_dev->pulse_to_period_ms =
      mrhat_dev->period_ms - mrhat_dev->pulse_width_ms;

  if (mrhat_dev->period_ms < 2 || mrhat_dev->pulse_to_period_ms < 1 ||
      mrhat_dev->pulse_to_period_ms < mrhat_dev->period_ms / 2 ||
      mrhat_dev->pulse_to_period_ms > mrhat_dev->period_ms) {
    dev_err(mrhat_dev->dev,
            "invalid heartbeat pulse settings received period:%u pulse:%u "
            "period2pulse:%u",
            mrhat_dev->period_ms, mrhat_dev->pulse_width_ms,
            mrhat_dev->pulse_to_period_ms);
    return -EINVAL;
  }

  mod_timer(&mrhat_dev->hb_timer,
            jiffies + msecs_to_jiffies(mrhat_dev->pulse_width_ms));

  dev_info(mrhat_dev->dev, "mrhat driver successfully initialized");
  return 0;
}

static struct platform_driver mrhat_driver = {
    .driver =
        {
            .name = "er-mrhat-plat",
            .of_match_table = mrhat_dt_ids,
        },
    .probe = er_mrhat_probe,
};

module_platform_driver(mrhat_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ferenc Janky");
MODULE_AUTHOR("Attila Gombos");
MODULE_DESCRIPTION(
    "Platform driver for the MrHATv1 family of RaspberryPi extension hats");
MODULE_VERSION("0.1.1");
