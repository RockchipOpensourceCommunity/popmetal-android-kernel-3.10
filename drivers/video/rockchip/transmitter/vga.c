#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/display-sys.h>
#include <linux/rk_screen.h>
#include <linux/rk_fb.h>
#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_gpio.h>
#endif

#include "../../edid.h"

#ifdef CONFIG_SWITCH
#include <linux/switch.h>
#endif
	
#define DDC_ADDR		0x50
#define DDC_I2C_RATE		100*1000
#define INVALID_GPIO		-1
#define GPIO_HIGH		1
#define GPIO_LOW		0
#define DISPLAY_SOURCE_LCDC0    0
#define DISPLAY_SOURCE_LCDC1    1

struct rockchip_vga {
	struct device 		 *dev;	/*i2c device*/
	struct rk_display_device *ddev; /*display device*/
	struct i2c_client 	 *client;
	struct list_head	 modelist;
	struct fb_monspecs	 specs;
	struct rk_screen	 screen;
	int 			 indx;
	int 			 en_pin;
	int 			 en_val;
	int 			 lcdc_id;
#ifdef CONFIG_SWITCH
	struct switch_dev	 switch_vga;
#endif
};

static int i2c_master_reg8_recv(const struct i2c_client *client,
		const char reg, char *buf, int count, int scl_rate)
{
        struct i2c_adapter *adap=client->adapter;
        struct i2c_msg msgs[2];
        int ret;
        char reg_buf = reg;

        msgs[0].addr = client->addr;
        msgs[0].flags = client->flags;
        msgs[0].len = 1;
        msgs[0].buf = &reg_buf;
        msgs[0].scl_rate = scl_rate;

        msgs[1].addr = client->addr;
        msgs[1].flags = client->flags | I2C_M_RD;
        msgs[1].len = count;
        msgs[1].buf = (char *)buf;
        msgs[1].scl_rate = scl_rate;

        ret = i2c_transfer(adap, msgs, 2);

        return (ret == 2)? count : ret;
}

static unsigned char *rk29fb_ddc_read(struct i2c_client *client)
{
	int rc;
	unsigned char *buf = kzalloc(EDID_LENGTH, GFP_KERNEL);
	if (!buf) {
		dev_err(&client->dev, "unable to allocate memory for EDID\n");
		return NULL;
	}
	
	/*Check ddc i2c communication is available or not*/
	rc = i2c_master_reg8_recv(client, 0, buf, 6, DDC_I2C_RATE);
	if (rc == 6) {
		memset(buf, 0, EDID_LENGTH);
		rc = i2c_master_reg8_recv(client, 0, buf, EDID_LENGTH, DDC_I2C_RATE);
		if(rc == EDID_LENGTH)
			return buf;
	}

	dev_err(&client->dev, "unable to read EDID block.\n");
	kfree(buf);
	return NULL;
}

static int vga_mode2screen(struct fb_videomode *modedb, struct rk_screen *screen)
{
	if(modedb == NULL || screen == NULL)
		return -1;
		
	memset(screen, 0, sizeof(struct rk_screen));
	memcpy(&screen->mode, modedb, sizeof(*modedb));
	screen->mode.pixclock = PICOS2KHZ(screen->mode.pixclock);
	screen->mode.pixclock /= 250;
	screen->mode.pixclock *= 250;
	screen->mode.pixclock *= 1000;
	screen->xsize = screen->mode.xres;
	screen->ysize = screen->mode.yres;

	screen->overscan.left = 100;
	screen->overscan.top = 100;
	screen->overscan.right = 100;
	screen->overscan.bottom = 100;
	/* screen type & face */
	screen->type = SCREEN_RGB;
	screen->face = OUT_P888;

	screen->pin_vsync = (screen->mode.sync & FB_SYNC_VERT_HIGH_ACT) ? 1 : 0;
	screen->pin_hsync = (screen->mode.sync & FB_SYNC_HOR_HIGH_ACT) ? 1 : 0;
	screen->pin_den = 0;
	screen->pin_dclk = 1;

	/* Swap rule */
	screen->swap_rb = 0;
	screen->swap_rg = 0;
	screen->swap_gb = 0;
	screen->swap_delta = 0;
	screen->swap_dumy = 0;
	/* Operation function*/
	screen->init = NULL;
	screen->standby = NULL;

	return 0;
}

static int vga_switch_screen(struct rockchip_vga *vga)
{
	struct fb_videomode *best_mode;
	struct rk_screen *screen = &vga->screen;
	struct fb_monspecs *specs = &vga->specs;
//	int i;

	/*
	 * best_mode VGA recommend from edid I think is first one.
	 * I guess...
	 */
	best_mode = &specs->modedb[0];
#if 0
	for (i = 0; i < specs->modedb_len; i++) {
		if (best_mode->yres * )
		if ((best_mode->yres * best_mode->xres) <
		    (specs->modedb[i].yres * specs->modedb[i].xres))
			best_mode = &specs->modedb[i];
	}
#endif

	vga_mode2screen(best_mode, screen);

	rk_fb_set_screen(screen);
//	rk_fb_switch_screen(screen, 1 ,vga->lcdc_id);

	return 0;
}

static int vga_get_screen_info(struct rockchip_vga *vga)
{
	u8 *edid;
	int i;
	struct fb_monspecs *specs = &vga->specs;
	struct list_head *modelist = &vga->modelist;
	edid = rk29fb_ddc_read(vga->client);
	if (!edid) {
		dev_info(vga->dev, "get edid failed!\n");
		return -EINVAL;
	}
	fb_edid_to_monspecs(edid,specs);
	INIT_LIST_HEAD(modelist);
	for (i = 0; i < specs->modedb_len; i++) {
		fb_add_videomode(&specs->modedb[i], modelist);

		pr_info("==================\n");
		pr_info("%dx%d@%d-%ld [<:%d >:%d ^:%d v:%d]\n",
			specs->modedb[i].xres, specs->modedb[i].yres,
			specs->modedb[i].refresh,
			(PICOS2KHZ(specs->modedb[i].pixclock)/250)*250*1000,
			specs->modedb[i].left_margin,
			specs->modedb[i].right_margin,
			specs->modedb[i].upper_margin,
			specs->modedb[i].lower_margin);

		pr_info("hpw[%d] vpw[%d] sync[%x vmode%x flag%x \n",
			specs->modedb[i].hsync_len,
			specs->modedb[i].vsync_len,
			specs->modedb[i].sync,
			specs->modedb[i].vmode,
			specs->modedb[i].flag);
	}
	return 0;
	
}

static int vga_get_modelist(struct rk_display_device *device,
			     struct list_head **modelist)
{
	struct rockchip_vga *vga = device->priv_data;
	*modelist = &vga->modelist;
	return 0;
}

static int vga_set_mode(struct rk_display_device *device,
			 struct fb_videomode *mode)
{
	struct rockchip_vga *vga = device->priv_data;
	struct rk_screen *screen = &vga->screen;
	vga_mode2screen(mode, screen);
	rk_fb_switch_screen(screen, 1 ,vga->lcdc_id);

	return 0;
}

static int vga_get_mode(struct rk_display_device *device,
			 struct fb_videomode *mode)
{
	return 0;
}

struct rk_display_ops vga_display_ops = {
	.getmodelist = vga_get_modelist,
	.setmode = vga_set_mode,
	.getmode = vga_get_mode,
};

static int vga_display_probe(struct rk_display_device *device, void *devdata)
{
	device->owner = THIS_MODULE;
	strcpy(device->type, "VGA");
	device->priority = DISPLAY_PRIORITY_VGA;
	device->priv_data = devdata;
	device->ops = &vga_display_ops;
	return 1;
}

static struct rk_display_driver display_vga = {
	.probe = vga_display_probe,
};

struct rk_display_device * vga_register_display_sysfs(struct rockchip_vga *vga)
{
	return rk_display_device_register(&display_vga, vga->dev, vga);
}

void vga_unregister_display_sysfs(struct rockchip_vga *vga)
{
	if (vga->ddev)
		rk_display_device_unregister(vga->ddev);
}

static int vga_i2c_probe(struct i2c_client *client,const struct i2c_device_id *id)
{    
	int ret;
	struct rockchip_vga *vga;
	struct device_node *np = client->dev.of_node;
	enum of_gpio_flags pwr_flags;

	if (!np) {
		dev_err(&client->dev, "no device node found!\n");
		return -EINVAL;
	} 

	vga = devm_kzalloc(&client->dev, sizeof(*vga), GFP_KERNEL);
	if (!vga) {
		dev_err(&client->dev, "allocate for vga failed!\n");
		return -ENOMEM;
	}

	vga->client = client;
	vga->dev = &client->dev;
	i2c_set_clientdata(client, vga);
	vga->ddev = vga_register_display_sysfs(vga);
	if (IS_ERR(vga->ddev))
		dev_warn(vga->dev, "Unable to create device for vga :%ld",
			PTR_ERR(vga->ddev));
	
	vga->en_pin = of_get_named_gpio_flags(np, "pwr_gpio", 0, &pwr_flags);
	if (gpio_is_valid(vga->en_pin)) {
		vga->en_val = (pwr_flags & OF_GPIO_ACTIVE_LOW) ? 0 : 1;
		ret = devm_gpio_request(vga->dev, vga->en_pin, "pwr_pin");
		if(ret < 0) {
			dev_err(vga->dev, "request for pwr_pin failed!\n ");
			goto err;
		}

		gpio_direction_output(vga->en_pin, vga->en_val);
	}

	vga->lcdc_id = DISPLAY_SOURCE_LCDC0;
	
	ret = vga_get_screen_info(vga);
	if (ret < 0)
		goto err;
	vga_switch_screen(vga);
	
	printk("VGA probe successful\n");
	return 0;
err:
	vga_unregister_display_sysfs(vga);
	return ret;
	
}

static int vga_i2c_remove(struct i2c_client *client)
{
	return 0;
}

#if defined(CONFIG_OF)
static struct of_device_id vga_dt_ids[] = {
	{.compatible = "rockchip,vga" },
	{ }
};
#endif

static const struct i2c_device_id vga_id[] = {
	{ "vga_i2c", 0 },
	{ }
};

static struct i2c_driver vga_i2c_driver  = {
	.driver = {
		.name  = "vga_i2c",
		.owner = THIS_MODULE,
#if defined(CONFIG_OF)
		.of_match_table = of_match_ptr(vga_dt_ids),
#endif
	},
	.probe		= &vga_i2c_probe,
	.remove		= &vga_i2c_remove,
	.id_table	= vga_id,
};

module_i2c_driver(vga_i2c_driver);
