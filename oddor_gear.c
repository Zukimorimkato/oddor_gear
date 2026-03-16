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
    struct mutex io_mutex;
    int open_count;
    unsigned char current_gear;
    int buffer_size;
    bool device_removed; // Flag to handle early device removal gracefully
    char phys[64];
};

static const struct usb_device_id oddor_gear_table[] = {
    { USB_DEVICE(ODDOR_GEAR_VENDOR_ID, ODDOR_GEAR_PRODUCT_ID) },
    { } /* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, oddor_gear_table);

static void oddor_gear_irq(struct urb *urb)
{
    struct oddor_gear *shifter = urb->context;
    int retval;
    unsigned char new_gear;
    int i;
    int prev_button;
    int new_button;

    if (!shifter || READ_ONCE(shifter->device_removed)) {
        return;
    }

    switch (urb->status) {
        case 0:            /* success */
            break;
        case -ECONNRESET:  /* unlink */
        case -ENOENT:
        case -ESHUTDOWN:
            return;
        default:
            goto resubmit;
    }

    if (!shifter->input || !shifter->input->users) {
        goto resubmit;
    }

    if (!shifter->data || urb->actual_length < 1) {
        dev_warn(&shifter->interface->dev, "Malformed packet received (length: %d)\n",
                urb->actual_length);
        goto resubmit;
    }

    new_gear = shifter->data[0];  /* Gear position is in byte 0 */

    /* Only process if gear changed */
    if (new_gear != shifter->current_gear) {
        /* Release previous gear */
        prev_button = -1;
        for (i = 0; i < GEAR_MAP_SIZE; i++) {
            if (shifter->current_gear == gear_button_map[i].gear_code) {
                prev_button = gear_button_map[i].button_code;
                break;
            }
        }
        if (prev_button != -1) {
            input_report_key(shifter->input, prev_button, 0);
        } else {
            /* If previous gear was unknown, release all to be safe */
            for (i = 0; i < GEAR_MAP_SIZE; i++) {
                input_report_key(shifter->input, gear_button_map[i].button_code, 0);
            }
        }

        /* Press new gear */
        new_button = -1;
        for (i = 0; i < GEAR_MAP_SIZE; i++) {
            if (new_gear == gear_button_map[i].gear_code) {
                new_button = gear_button_map[i].button_code;
                pr_info("ODDOR-GEAR: %s engaged (0x%02X)\n",
                        gear_button_map[i].name, new_gear);
                break;
            }
        }
        if (new_button != -1) {
            input_report_key(shifter->input, new_button, 1);
        } else if (new_gear == GEAR_NEUTRAL) {
            pr_info("ODDOR-GEAR: Neutral (0x%02X)\n", new_gear);
        }

        shifter->current_gear = new_gear;
        input_sync(shifter->input);
    }

resubmit:
    retval = usb_submit_urb(urb, GFP_ATOMIC);
    if (retval) {
        dev_err(&shifter->interface->dev,
                "%s - usb_submit_urb failed with result %d\n",
                __func__, retval);
    }
}

static int oddor_gear_open(struct input_dev *dev)
{
    struct oddor_gear *shifter = input_get_drvdata(dev);
    int retval;

    mutex_lock(&shifter->io_mutex);

    if (shifter->device_removed) {
        mutex_unlock(&shifter->io_mutex);
        return -ENODEV;
    }

    if (shifter->open_count++ == 0) {
        shifter->irq_urb->dev = shifter->udev;
        retval = usb_submit_urb(shifter->irq_urb, GFP_KERNEL);
        if (retval) {
            shifter->open_count--;
            mutex_unlock(&shifter->io_mutex);
            return retval;
        }
    }

    mutex_unlock(&shifter->io_mutex);
    return 0;
}

static void oddor_gear_close(struct input_dev *dev)
{
    struct oddor_gear *shifter = input_get_drvdata(dev);

    mutex_lock(&shifter->io_mutex);

    if (--shifter->open_count == 0) {
        usb_kill_urb(shifter->irq_urb);
    }

    mutex_unlock(&shifter->io_mutex);
}

static int oddor_gear_probe(struct usb_interface *interface,
                           const struct usb_device_id *id)
{
    struct usb_device *udev = interface_to_usbdev(interface);
    struct oddor_gear *shifter;
    struct usb_endpoint_descriptor *endpoint;
    struct usb_host_interface *iface_desc;
    struct input_dev *input_dev;
    int i, pipe, maxp, error = -ENOMEM;

    shifter = kzalloc(sizeof(struct oddor_gear), GFP_KERNEL);
    if (!shifter)
        return -ENOMEM;

    shifter->udev = usb_get_dev(udev);
    shifter->interface = interface;
    mutex_init(&shifter->io_mutex);
    shifter->current_gear = GEAR_NEUTRAL;
    shifter->buffer_size = 0;
    shifter->device_removed = false;

    // Find interrupt endpoint
    iface_desc = interface->cur_altsetting;
    if (!iface_desc) {
        error = -ENODEV;
        goto err_free_shifter;
    }
    for (i = 0; i < iface_desc->desc.bNumEndpoints; i++) {
        endpoint = &iface_desc->endpoint[i].desc;
        if (usb_endpoint_is_int_in(endpoint)) {
            pipe = usb_rcvintpipe(udev, endpoint->bEndpointAddress);
            maxp = usb_maxpacket(udev, pipe);
            break;
        }
    }

    if (i == iface_desc->desc.bNumEndpoints) {
        dev_err(&interface->dev, "No interrupt endpoint found\n");
        error = -ENODEV;
        goto err_free_shifter;
    }

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
    input_free_device(input_dev);
err_free_urb:
    usb_free_urb(shifter->irq_urb);
err_free_buffer:
    usb_free_coherent(udev, maxp, shifter->data, shifter->data_dma);
err_free_shifter:
    kfree(shifter);
    return error;
}

static void oddor_gear_disconnect(struct usb_interface *interface)
{
    struct oddor_gear *shifter = usb_get_intfdata(interface);

    if (!shifter) {
        return;
    }

    usb_set_intfdata(interface, NULL);

    WRITE_ONCE(shifter->device_removed, true);

    usb_kill_urb(shifter->irq_urb);

    if (shifter->input)
        input_unregister_device(shifter->input);

    usb_free_urb(shifter->irq_urb);

    if (shifter->data) {
        usb_free_coherent(shifter->udev, shifter->buffer_size,
                        shifter->data, shifter->data_dma);
    }

    dev_info(&interface->dev, "ZSC ODDOR-GEAR disconnected\n");

    usb_put_dev(shifter->udev);
    mutex_destroy(&shifter->io_mutex);
    kfree(shifter);
}

static struct usb_driver oddor_gear_driver = {
    .name = "oddor_gear",
    .probe = oddor_gear_probe,
    .disconnect = oddor_gear_disconnect,
    .id_table = oddor_gear_table,
    .supports_autosuspend = 1,
};

module_usb_driver(oddor_gear_driver);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE(DRIVER_LICENSE);
