/* SPDX-License-Identifier: GPL-2.0 */
/*
 *
 */

//project para struct: static, config by developer
struct project_current_config_struct {
    int sysui_torch[2];//sysui torch, [0]low1 level current, [1]low2 level current
    int fac_flash;//factory mode current
    int faceid_torch;//face id current
    int torch_360;//torch_360 current
    int phonecall;//phonecall reminder current
    int torch_duty_range[2];//slide control torch current, [0]min, [1]max
};

struct project_current_config_struct g_project_current_config[3] = {
    {
        {124,210},
        600,
        22,
        180,
        22,
        {110,227},
    },
    {
        {182,182},
        235,
        182,
        182,
        182,
        {120,220}
    },
    {
        {124,210},
        600,
        22,
        180,
        22,
        {110,227},
    },

};

