#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/mutex.h>

#define DRIVER_AUTHOR "ODDOR-GEAR Driver"
#define DRIVER_DESC "ZSC ODDOR-GEAR USB Shifter Driver"
#define DRIVER_LICENSE "GPL"

// ZSC ODDOR-GEAR specific VID/PID
#define ODDOR_GEAR_VENDOR_ID  0x4785
#define ODDOR_GEAR_PRODUCT_ID 0x7353

// Gear position codes
#define GEAR_NEUTRAL   0x00
#define GEAR_1         0x01
#define GEAR_2         0x02
#define GEAR_3         0x04
#define GEAR_4         0x08
#define GEAR_5         0x10
#define GEAR_6         0x20
#define GEAR_7         0x40
#define GEAR_R         0x80

// Button mappings for joystick
#define BTN_GEAR_1     BTN_TRIGGER_HAPPY1
#define BTN_GEAR_2     BTN_TRIGGER_HAPPY2
#define BTN_GEAR_3     BTN_TRIGGER_HAPPY3
#define BTN_GEAR_4     BTN_TRIGGER_HAPPY4
#define BTN_GEAR_5     BTN_TRIGGER_HAPPY5
#define BTN_GEAR_6     BTN_TRIGGER_HAPPY6
#define BTN_GEAR_7     BTN_TRIGGER_HAPPY7
#define BTN_GEAR_R     BTN_TRIGGER_HAPPY8

// Input device name
#define DEVICE_NAME    "ZSC ODDOR-GEAR Shifter"

// Gear to button mapping lookup table
static const struct {
    unsigned char gear_code;
    unsigned int button_code;
    const char *name;
} gear_button_map[] = {
    { GEAR_1, BTN_GEAR_1, "Gear 1" },
    { GEAR_2, BTN_GEAR_2, "Gear 2" },
    { GEAR_3, BTN_GEAR_3, "Gear 3" },
    { GEAR_4, BTN_GEAR_4, "Gear 4" },
    { GEAR_5, BTN_GEAR_5, "Gear 5" },
    { GEAR_6, BTN_GEAR_6, "Gear 6" },
    { GEAR_7, BTN_GEAR_7, "Gear 7" },
    { GEAR_R, BTN_GEAR_R, "Reverse" },
};

#define GEAR_MAP_SIZE ARRAY_SIZE(gear_button_map)

struct oddor_gear {
    struct usb_device *udev;
    struct usb_interface *interface;
    struct input_dev *input;
    unsigned char *data;
    dma_addr_t data_dma;
    struct urb *irq_urb;
    /*
     * Protects open_count, suspended and device_removed, and serialises
     * URB submission/kill between open/close/suspend/resume/disconnect.
     * Process context only: never take it from the URB completion handler.
     */
    struct mutex io_mutex;
    int open_count;
    unsigned char current_gear;   /* only touched by the completion handler */
    int buffer_size;
    bool device_removed;          /* set in disconnect */
    bool suspended;               /* set between suspend and resume */
    char phys[64];
};

static const struct usb_device_id oddor_gear_table[] = {
    { USB_DEVICE(ODDOR_GEAR_VENDOR_ID, ODDOR_GEAR_PRODUCT_ID) },
    { } /* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, oddor_gear_table);

/* Returns the index in gear_button_map for @gear, or -1 if not a single gear. */
static int oddor_gear_lookup(unsigned char gear)
{
    unsigned int i;

    for (i = 0; i < GEAR_MAP_SIZE; i++) {
        if (gear_button_map[i].gear_code == gear)
            return i;
    }
    return -1;
}

/*
 * URB completion handler. Runs in atomic (interrupt / BH) context, so it
 * must not sleep: no mutexes, GFP_ATOMIC only. The input core's
 * input_report_key()/input_sync() are safe to call from here.
 */
static void oddor_gear_irq(struct urb *urb)
{
    struct oddor_gear *shifter = urb->context;
    struct device *dev = &shifter->interface->dev;
    unsigned char new_gear;
    unsigned int i;
    int prev_idx, new_idx;
    int retval;

    switch (urb->status) {
        case 0:            /* success */
            break;
        case -ECONNRESET:  /* unlinked / killed */
        case -ENOENT:
        case -ESHUTDOWN:   /* device or host controller gone */
            return;
        default:           /* transient error (e.g. -EPROTO, -EILSEQ): retry */
            dev_dbg_ratelimited(dev, "nonzero urb status %d\n", urb->status);
            goto resubmit;
    }

    if (READ_ONCE(shifter->device_removed))
        return;

    if (urb->actual_length < 1) {
        dev_warn_ratelimited(dev, "Malformed packet received (length: %d)\n",
                             urb->actual_length);
        goto resubmit;
    }

    new_gear = shifter->data[0];  /* Gear position is in byte 0 */

    /* Only process if gear changed */
    if (new_gear != shifter->current_gear) {
        /* Release previous gear */
        prev_idx = oddor_gear_lookup(shifter->current_gear);
        if (prev_idx >= 0) {
            input_report_key(shifter->input,
                             gear_button_map[prev_idx].button_code, 0);
        } else {
            /* If previous gear was unknown, release all to be safe */
            for (i = 0; i < GEAR_MAP_SIZE; i++)
                input_report_key(shifter->input,
                                 gear_button_map[i].button_code, 0);
        }

        /* Press new gear */
        new_idx = oddor_gear_lookup(new_gear);
        if (new_idx >= 0) {
            input_report_key(shifter->input,
                             gear_button_map[new_idx].button_code, 1);
            dev_dbg(dev, "%s engaged (0x%02X)\n",
                    gear_button_map[new_idx].name, new_gear);
        } else if (new_gear == GEAR_NEUTRAL) {
            dev_dbg(dev, "Neutral (0x%02X)\n", new_gear);
        } else {
            dev_dbg(dev, "Unknown gear code 0x%02X\n", new_gear);
        }

        shifter->current_gear = new_gear;
        input_sync(shifter->input);
    }

resubmit:
    retval = usb_submit_urb(urb, GFP_ATOMIC);
    /* -EPERM: URB is being killed; -ENODEV: device is gone. Both expected. */
    if (retval && retval != -EPERM && retval != -ENODEV)
        dev_err_ratelimited(dev, "%s - usb_submit_urb failed with result %d\n",
                            __func__, retval);
}

static int oddor_gear_open(struct input_dev *dev)
{
    struct oddor_gear *shifter = input_get_drvdata(dev);
    int retval = 0;

    mutex_lock(&shifter->io_mutex);

    if (shifter->device_removed) {
        retval = -ENODEV;
        goto out;
    }

    /* While suspended, resume() will submit the URB for us. */
    if (shifter->open_count == 0 && !shifter->suspended) {
        retval = usb_submit_urb(shifter->irq_urb, GFP_KERNEL);
        if (retval)
            goto out;
    }
    shifter->open_count++;

out:
    mutex_unlock(&shifter->io_mutex);
    return retval;
}

static void oddor_gear_close(struct input_dev *dev)
{
    struct oddor_gear *shifter = input_get_drvdata(dev);

    mutex_lock(&shifter->io_mutex);

    if (shifter->open_count > 0 && --shifter->open_count == 0)
        usb_kill_urb(shifter->irq_urb);

    mutex_unlock(&shifter->io_mutex);
}

static int oddor_gear_probe(struct usb_interface *interface,
                           const struct usb_device_id *id)
{
    struct usb_device *udev = interface_to_usbdev(interface);
    struct oddor_gear *shifter;
    struct usb_endpoint_descriptor *endpoint;
    struct input_dev *input_dev;
    unsigned int i;
    int pipe, maxp, error;

    // Find interrupt IN endpoint (validates descriptors from the device)
    error = usb_find_int_in_endpoint(interface->cur_altsetting, &endpoint);
    if (error) {
        dev_err(&interface->dev, "No interrupt endpoint found\n");
        return -ENODEV;
    }

    pipe = usb_rcvintpipe(udev, endpoint->bEndpointAddress);
    maxp = usb_endpoint_maxp(endpoint);
    if (maxp < 1) {
        dev_err(&interface->dev, "Invalid endpoint max packet size %d\n", maxp);
        return -ENODEV;
    }

    shifter = kzalloc(sizeof(*shifter), GFP_KERNEL);
    if (!shifter)
        return -ENOMEM;

    shifter->udev = usb_get_dev(udev);
    shifter->interface = interface;
    mutex_init(&shifter->io_mutex);
    shifter->current_gear = GEAR_NEUTRAL;

    // Allocate data buffer for USB transfers
    shifter->data = usb_alloc_coherent(udev, maxp, GFP_KERNEL,
                                      &shifter->data_dma);
    if (!shifter->data) {
        error = -ENOMEM;
        goto err_free_shifter;
    }
    shifter->buffer_size = maxp;

    // Allocate URB
    shifter->irq_urb = usb_alloc_urb(0, GFP_KERNEL);
    if (!shifter->irq_urb) {
        error = -ENOMEM;
        goto err_free_buffer;
    }

    usb_fill_int_urb(shifter->irq_urb, udev, pipe,
                    shifter->data, maxp,
                    oddor_gear_irq, shifter,
                    endpoint->bInterval);
    shifter->irq_urb->transfer_dma = shifter->data_dma;
    shifter->irq_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

    // Create input device
    input_dev = input_allocate_device();
    if (!input_dev) {
        error = -ENOMEM;
        goto err_free_urb;
    }

    shifter->input = input_dev;
    input_dev->name = DEVICE_NAME;
    usb_make_path(udev, shifter->phys, sizeof(shifter->phys));
    strlcat(shifter->phys, "/input0", sizeof(shifter->phys));
    input_dev->phys = shifter->phys;
    input_dev->id.bustype = BUS_USB;
    input_dev->id.vendor = ODDOR_GEAR_VENDOR_ID;
    input_dev->id.product = ODDOR_GEAR_PRODUCT_ID;
    input_dev->id.version = 0x0100;
    input_dev->dev.parent = &interface->dev;

    // Set up as a button device only (no EV_ABS since we don't have axes)
    __set_bit(EV_KEY, input_dev->evbit);

    // Set up gear buttons using lookup table
    for (i = 0; i < GEAR_MAP_SIZE; i++) {
        __set_bit(gear_button_map[i].button_code, input_dev->keybit);
    }

    input_dev->open = oddor_gear_open;
    input_dev->close = oddor_gear_close;

    input_set_drvdata(input_dev, shifter);
    usb_set_intfdata(interface, shifter);

    error = input_register_device(input_dev);
    if (error) {
        dev_err(&interface->dev,
                "Failed to register input device: %d\n", error);
        goto err_free_input;
    }

    dev_info(&interface->dev,
             "ZSC ODDOR-GEAR H-pattern shifter connected as joystick\n");

    return 0;

err_free_input:
    usb_set_intfdata(interface, NULL);
    input_free_device(input_dev);
err_free_urb:
    usb_free_urb(shifter->irq_urb);
err_free_buffer:
    usb_free_coherent(udev, maxp, shifter->data, shifter->data_dma);
err_free_shifter:
    usb_put_dev(shifter->udev);
    mutex_destroy(&shifter->io_mutex);
    kfree(shifter);
    return error;
}

static void oddor_gear_disconnect(struct usb_interface *interface)
{
    struct oddor_gear *shifter = usb_get_intfdata(interface);

    if (!shifter) {
        return;
    }

    mutex_lock(&shifter->io_mutex);
    WRITE_ONCE(shifter->device_removed, true);
    mutex_unlock(&shifter->io_mutex);

    /*
     * Must NOT hold io_mutex here: input_unregister_device() calls our
     * close() callback for any still-open handles, which takes io_mutex.
     * After this returns no open()/close() can run any more.
     */
    input_unregister_device(shifter->input);

    /* close() already killed it if it was running; this is just a safety net. */
    usb_kill_urb(shifter->irq_urb);
    usb_free_urb(shifter->irq_urb);

    usb_free_coherent(shifter->udev, shifter->buffer_size,
                      shifter->data, shifter->data_dma);

    usb_set_intfdata(interface, NULL);

    dev_info(&interface->dev, "ZSC ODDOR-GEAR disconnected\n");

    usb_put_dev(shifter->udev);
    mutex_destroy(&shifter->io_mutex);
    kfree(shifter);
}

static int oddor_gear_suspend(struct usb_interface *interface,
                              pm_message_t message)
{
    struct oddor_gear *shifter = usb_get_intfdata(interface);

    mutex_lock(&shifter->io_mutex);
    shifter->suspended = true;
    usb_kill_urb(shifter->irq_urb);
    mutex_unlock(&shifter->io_mutex);

    return 0;
}

static int oddor_gear_resume(struct usb_interface *interface)
{
    struct oddor_gear *shifter = usb_get_intfdata(interface);
    int retval = 0;

    mutex_lock(&shifter->io_mutex);
    shifter->suspended = false;
    if (shifter->open_count > 0 && !shifter->device_removed)
        retval = usb_submit_urb(shifter->irq_urb, GFP_NOIO);
    mutex_unlock(&shifter->io_mutex);

    return retval;
}

static struct usb_driver oddor_gear_driver = {
    .name = "oddor_gear",
    .probe = oddor_gear_probe,
    .disconnect = oddor_gear_disconnect,
    .suspend = oddor_gear_suspend,
    .resume = oddor_gear_resume,
    .reset_resume = oddor_gear_resume,  /* device keeps no state to restore */
    .id_table = oddor_gear_table,
};

module_usb_driver(oddor_gear_driver);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE(DRIVER_LICENSE);
