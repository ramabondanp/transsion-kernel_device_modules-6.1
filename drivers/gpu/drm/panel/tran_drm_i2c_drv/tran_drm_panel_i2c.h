#ifndef __TRAN_DRM_PANEL_I2C_H__
#define __TRAN_DRM_PANEL_I2C_H__
/* function: write i2c data
 * input: addr: i2c address, value: i2c value
 * output: 0 for success; !0 for failed
 */
int tran_panel_i2c_write_bytes(unsigned char addr, unsigned char value);
int tran_panel_i2c_read_bytes(unsigned char addr, unsigned char *value);
extern uint8_t g_shutdown_flag;

#endif
