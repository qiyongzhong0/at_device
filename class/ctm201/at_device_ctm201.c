/*
 * File      : at_device_ctm201.c
 * This file is part of RT-Thread RTOS
 * COPYRIGHT (C) 2006 - 2018, RT-Thread Development Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Change Logs:
 * Date           Author       Notes
 * 2019-12-30     qiyongzhong  first version
 */

#include <stdio.h>
#include <string.h>

#include <at_device_ctm201.h>

#define LOG_TAG                         "at.dev.ctm201"
#include <at_log.h>

#ifdef AT_DEVICE_USING_CTM201

#ifndef CTM201_USING_POWER_CTRL
#define CTM201_USING_POWER_CTRL           0//module support power control
#endif

#define CTM201_WAIT_CONNECT_TIME          5000
#define CTM201_THREAD_STACK_SIZE          2048
#define CTM201_THREAD_PRIORITY            (RT_THREAD_PRIORITY_MAX/2)

static int ctm201_power_on(struct at_device *device)
{
    struct at_device_ctm201 *ctm201 = RT_NULL;
    
    ctm201 = (struct at_device_ctm201 *)device->user_data;
    ctm201->power_status = RT_TRUE;

    #if CTM201_USING_POWER_CTRL
    if (ctm201->power_pin == -1)
    {
        return(RT_EOK);
    }
    rt_pin_write(ctm201->power_pin, PIN_HIGH);
    #endif
    
    LOG_D("power on success.");

    return(RT_EOK);
}

static int ctm201_power_off(struct at_device *device)
{
    struct at_device_ctm201 *ctm201 = RT_NULL;
    
    ctm201 = (struct at_device_ctm201 *)device->user_data;
    ctm201->power_status = RT_FALSE;

    #if CTM201_USING_POWER_CTRL
    if (ctm201->power_pin == -1)
    {
        return(RT_EOK);
    }
    rt_pin_write(ctm201->power_pin, PIN_LOW);
    #endif
    
    LOG_D("power off success.");

    return(RT_EOK);
}

static int ctm201_reset(struct at_device *device)
{
    at_response_t resp = RT_NULL;
    struct at_device_ctm201 *ctm201 = RT_NULL;

    ctm201 = (struct at_device_ctm201 *)device->user_data;
    
    #if CTM201_USING_POWER_CTRL
    if ( ! ctm201->power_status)//power off
    {
        LOG_E("the power is off and the reset cannot be performed");
        return(-RT_ERROR);
    }
    #endif
    
    resp = at_create_resp(64, 0, rt_tick_from_millisecond(300));
    if (resp == RT_NULL)
    {
        LOG_E("no memory for resp create.");
        return(-RT_ERROR);
    }
    
    at_obj_exec_cmd(device->client, resp, "AT+NRB");//command no response "OK"
    
    at_delete_resp(resp);
    
    LOG_D("reset success.");
    
    return(RT_EOK);
}

static int ctm201_sleep(struct at_device *device)
{
    at_response_t resp = RT_NULL;
    struct at_device_ctm201 *ctm201 = RT_NULL;
    
    ctm201 = (struct at_device_ctm201 *)device->user_data;
    if ( ! ctm201->power_status)//power off
    {
        return(RT_EOK);
    }
    if (ctm201->sleep_status)//is sleep status 
    {
        return(RT_EOK);
    }
    
    resp = at_create_resp(64, 0, rt_tick_from_millisecond(300));
    if (resp == RT_NULL)
    {
        LOG_E("no memory for resp create.");
        return(-RT_ERROR);
    }
    
    if (at_obj_exec_cmd(device->client, resp, "AT+CPSMS=1,,,00111110,00000001") != RT_EOK)
    {
        LOG_D("enable sleep fail.");
        at_delete_resp(resp);
        return(-RT_ERROR);
    }
    
    at_delete_resp(resp);
    ctm201->sleep_status = RT_TRUE;
    
    LOG_D("sleep success.");
    
    return(RT_EOK);
}

static int ctm201_wakeup(struct at_device *device)
{
    at_response_t resp = RT_NULL;
    struct at_device_ctm201 *ctm201 = RT_NULL;

    ctm201 = (struct at_device_ctm201 *)device->user_data;
    if ( ! ctm201->power_status)//power off
    {
        LOG_E("the power is off and the wake-up cannot be performed");
        return(-RT_ERROR);
    }
    if ( ! ctm201->sleep_status)//no sleep status
    {
        return(RT_EOK);
    }
    
    resp = at_create_resp(64, 0, rt_tick_from_millisecond(300));
    if (resp == RT_NULL)
    {
        LOG_E("no memory for resp create.");
        return(-RT_ERROR);
    }
    
    if (at_obj_exec_cmd(device->client, resp, "AT+CPSMS=0") != RT_EOK)
    {
        LOG_D("wake up fail.");
        at_delete_resp(resp);
        return(-RT_ERROR);
    }
    
    at_delete_resp(resp);
    ctm201->sleep_status = RT_FALSE;
    
    LOG_D("wake up success.");
    
    return(RT_EOK);
}

static int ctm201_check_link_status(struct at_device *device)
{
    at_response_t resp = RT_NULL;
    struct at_device_ctm201 *ctm201 = RT_NULL;
    int result = -RT_ERROR;

    RT_ASSERT(device);

    ctm201 = (struct at_device_ctm201 *)device->user_data;
    if ( ! ctm201->power_status)//power off
    {
        LOG_D("the power is off.");
        return(-RT_ERROR);
    }
    
    resp = at_create_resp(64, 0, rt_tick_from_millisecond(300));
    if (resp == RT_NULL)
    {
        LOG_E("no memory for resp create.");
        return(-RT_ERROR);
    }

    result = -RT_ERROR;
    if (at_obj_exec_cmd(device->client, resp, "AT+CEREG?") == RT_EOK)
    {
        int link_stat = 0;
        if (at_resp_parse_line_args_by_kw(resp, "+CEREG:", "+CEREG: %*d,%d", &link_stat) > 0)
        {
            if (link_stat == 1 || link_stat == 5)
            {
                result = RT_EOK;
            }
        }
    }
    
    at_delete_resp(resp);
    
    return(result);
}


/* =============================  ctm201 network interface operations ============================= */
/* set ctm201 network interface device status and address information */
static int ctm201_netdev_set_info(struct netdev *netdev)
{
#define CTM201_INFO_RESP_SIZE      128
#define CTM201_INFO_RESP_TIMO      rt_tick_from_millisecond(300)

    int result = RT_EOK;
    ip_addr_t addr;
    at_response_t resp = RT_NULL;
    struct at_device *device = RT_NULL;

    RT_ASSERT(netdev);

    device = at_device_get_by_name(AT_DEVICE_NAMETYPE_NETDEV, netdev->name);
    if (device == RT_NULL)
    {
        LOG_E("get device(%s) failed.", netdev->name);
        return -RT_ERROR;
    }

    /* set network interface device status */
    netdev_low_level_set_status(netdev, RT_TRUE);
    netdev_low_level_set_link_status(netdev, RT_TRUE);
    netdev_low_level_set_dhcp_status(netdev, RT_TRUE);

    resp = at_create_resp(CTM201_INFO_RESP_SIZE, 0, CTM201_INFO_RESP_TIMO);
    if (resp == RT_NULL)
    {
        LOG_E("no memory for resp create.");
        result = -RT_ENOMEM;
        goto __exit;
    }

    /* set network interface device hardware address(IMEI) */
    {
        #define CTM201_NETDEV_HWADDR_LEN   8
        #define CTM201_IMEI_LEN            15

        char imei[CTM201_IMEI_LEN] = {0};
        int i = 0, j = 0;

        /* send "AT+GSN" commond to get device IMEI */
        if (at_obj_exec_cmd(device->client, resp, "AT+CGSN=1") != RT_EOK)
        {
            result = -RT_ERROR;
            goto __exit;
        }
        
        if (at_resp_parse_line_args_by_kw(resp, "+CGSN:", "+CGSN:%s", imei) <= 0)
        {
            LOG_E("%s device prase \"AT+CGSN=1\" cmd error.", device->name);
            result = -RT_ERROR;
            goto __exit;
        }
        
        LOG_D("%s device IMEI number: %s", device->name, imei);

        netdev->hwaddr_len = CTM201_NETDEV_HWADDR_LEN;
        /* get hardware address by IMEI */
        for (i = 0, j = 0; i < CTM201_NETDEV_HWADDR_LEN && j < CTM201_IMEI_LEN; i++, j+=2)
        {
            if (j != CTM201_IMEI_LEN - 1)
            {
                netdev->hwaddr[i] = (imei[j] - '0') * 10 + (imei[j + 1] - '0');
            }
            else
            {
                netdev->hwaddr[i] = (imei[j] - '0');
            }
        }
    }

    /* set network interface device IP address */
    {
        #define IP_ADDR_SIZE_MAX    16
        char ipaddr[IP_ADDR_SIZE_MAX] = {0};
        
        /* send "AT+CGPADDR=1" commond to get IP address */
        if (at_obj_exec_cmd(device->client, resp, "AT+CGPADDR=0") != RT_EOK)
        {
            result = -RT_ERROR;
            goto __exit;
        }

        /* parse response data "+CGPADDR: 1,<IP_address>" */
        if (at_resp_parse_line_args_by_kw(resp, "+CGPADDR:", "+CGPADDR: %*d,%s", ipaddr) <= 0)
        {
            LOG_E("%s device \"AT+CGPADDR=1\" cmd error.", device->name);
            result = -RT_ERROR;
            goto __exit;
        }

        LOG_D("%s device IP address: %s", device->name, ipaddr);

        /* set network interface address information */
        inet_aton(ipaddr, &addr);
        netdev_low_level_set_ipaddr(netdev, &addr);
    }
    
__exit:
    if (resp)
    {
        at_delete_resp(resp);
    }

    return result;
}

static void ctm201_check_link_status_entry(void *parameter)
{
#define CTM201_LINK_DELAY_TIME    (60 * RT_TICK_PER_SECOND)

    rt_bool_t is_link_up;
    struct at_device *device = RT_NULL;
    struct netdev *netdev = (struct netdev *) parameter;

    device = at_device_get_by_name(AT_DEVICE_NAMETYPE_NETDEV, netdev->name);
    if (device == RT_NULL)
    {
        LOG_E("get device(%s) failed.", netdev->name);
        return;
    }
    
    while (1)
    {
        is_link_up = (ctm201_check_link_status(device) == RT_EOK);

        netdev_low_level_set_link_status(netdev, is_link_up);

        rt_thread_delay(CTM201_LINK_DELAY_TIME);
    }
}

static int ctm201_netdev_check_link_status(struct netdev *netdev)
{
#define CTM201_LINK_THREAD_TICK           20
#define CTM201_LINK_THREAD_STACK_SIZE     (1024 + 512)
#define CTM201_LINK_THREAD_PRIORITY       (RT_THREAD_PRIORITY_MAX - 2)

    rt_thread_t tid;
    char tname[RT_NAME_MAX] = {0};

    RT_ASSERT(netdev);

    rt_snprintf(tname, RT_NAME_MAX, "%s", netdev->name);

    /* create ctm201 link status polling thread  */
    tid = rt_thread_create(tname, ctm201_check_link_status_entry, (void *)netdev,
                           CTM201_LINK_THREAD_STACK_SIZE, CTM201_LINK_THREAD_PRIORITY, CTM201_LINK_THREAD_TICK);
    if (tid != RT_NULL)
    {
        rt_thread_startup(tid);
    }

    return RT_EOK;
}

static int ctm201_net_init(struct at_device *device);

static int ctm201_netdev_set_up(struct netdev *netdev)
{
    struct at_device *device = RT_NULL;

    device = at_device_get_by_name(AT_DEVICE_NAMETYPE_NETDEV, netdev->name);
    if (device == RT_NULL)
    {
        LOG_E("get device(%s) failed.", netdev->name);
        return -RT_ERROR;
    }

    if (device->is_init == RT_FALSE)
    {
        ctm201_net_init(device);
        device->is_init = RT_TRUE;

        netdev_low_level_set_status(netdev, RT_TRUE);
        LOG_D("network interface device(%s) set up status.", netdev->name);
    }

    return RT_EOK;
}

static int ctm201_netdev_set_down(struct netdev *netdev)
{
    struct at_device *device = RT_NULL;

    device = at_device_get_by_name(AT_DEVICE_NAMETYPE_NETDEV, netdev->name);
    if (device == RT_NULL)
    {
        LOG_E("get device(%s) failed.", netdev->name);
        return -RT_ERROR;
    }

    if (device->is_init == RT_TRUE)
    {
        ctm201_power_off(device);
        device->is_init = RT_FALSE;

        netdev_low_level_set_status(netdev, RT_FALSE);
        LOG_D("network interface device(%s) set down status.", netdev->name);
    }

    return RT_EOK;
}

#ifdef NETDEV_USING_PING
static int ctm201_netdev_ping(struct netdev *netdev, const char *host,
        size_t data_len, uint32_t timeout, struct netdev_ping_resp *ping_resp)
{
#define CTM201_PING_RESP_SIZE       128
#define CTM201_PING_IP_SIZE         16
#define CTM201_PING_TIMEO           (10 * RT_TICK_PER_SECOND)

    rt_err_t result = RT_EOK;
    int response = -1, recv_data_len, ping_time, ttl;
    char ip_addr[CTM201_PING_IP_SIZE] = {0};
    at_response_t resp = RT_NULL;
    struct at_device *device = RT_NULL;

    RT_ASSERT(netdev);
    RT_ASSERT(host);
    RT_ASSERT(ping_resp);

    device = at_device_get_by_name(AT_DEVICE_NAMETYPE_NETDEV, netdev->name);
    if (device == RT_NULL)
    {
        LOG_E("get device(%s) failed.", netdev->name);
        return -RT_ERROR;
    }

    if (timeout <= 0)
    {
        timeout = CTM201_PING_TIMEO;
    }
    resp = at_create_resp(CTM201_PING_RESP_SIZE, 4, timeout);
    if (resp == RT_NULL)
    {
        LOG_E("no memory for resp create");
        return -RT_ENOMEM;
    }

    if (at_obj_exec_cmd(device->client, resp, "AT+NPING=%s,%d,%d", host, data_len, timeout) != RT_EOK)
    {
        result = -RT_ERROR;
        goto __exit;
    }

    if (at_resp_parse_line_args_by_kw(resp, "+NPING::", "+NPING:%s,%d,%d", ip_addr, &ttl, &ping_time) <= 0)
    {
        result = -RT_ERROR;
        goto __exit;
    }

    inet_aton(ip_addr, &(ping_resp->ip_addr));
    ping_resp->data_len = data_len;
    ping_resp->ticks = ping_time;
    ping_resp->ttl = ttl;

__exit:
    if (resp)
    {
        at_delete_resp(resp);
    }

    return result;
}
#endif /* NETDEV_USING_PING */

const struct netdev_ops ctm201_netdev_ops =
{
    ctm201_netdev_set_up,
    ctm201_netdev_set_down,

    RT_NULL,
    RT_NULL,
    RT_NULL,

#ifdef NETDEV_USING_PING
    ctm201_netdev_ping,
    RT_NULL,
#endif
    RT_NULL,
};

static struct netdev *ctm201_netdev_add(const char *netdev_name)
{
#define ETHERNET_MTU        1500
#define HWADDR_LEN          8
    struct netdev *netdev = RT_NULL;

    netdev = netdev_get_by_name(netdev_name);
    if(netdev != RT_NULL)
    {
        return(netdev);
    }
    
    netdev = (struct netdev *)rt_calloc(1, sizeof(struct netdev));
    if (netdev == RT_NULL)
    {
        LOG_E("no memory for netdev create.");
        return RT_NULL;
    }

    netdev->mtu = ETHERNET_MTU;
    netdev->ops = &ctm201_netdev_ops;
    netdev->hwaddr_len = HWADDR_LEN;

#ifdef SAL_USING_AT
    extern int sal_at_netdev_set_pf_info(struct netdev *netdev);
    /* set the network interface socket/netdb operations */
    sal_at_netdev_set_pf_info(netdev);
#endif

    netdev_register(netdev, netdev_name, RT_NULL);

    return netdev;
}

/* =============================  ctm201 device operations ============================= */

/* initialize for ctm201 */
static void ctm201_init_thread_entry(void *parameter)
{
#define INIT_RETRY                     5
#define CPIN_RETRY                     10
#define CSQ_RETRY                      20
#define CGREG_RETRY                    60
#define IPADDR_RETRY                   10

    int i;
    int retry_num = INIT_RETRY;
    rt_err_t result = RT_EOK;
    at_response_t resp = RT_NULL;
    struct at_device *device = (struct at_device *) parameter;
    struct at_client *client = device->client;

    resp = at_create_resp(256, 0, rt_tick_from_millisecond(500));
    if (resp == RT_NULL)
    {
        LOG_E("no memory for resp create.");
        return;
    }

    LOG_D("start init %s device.", device->name);

    while (retry_num--)
    {
        ctm201_power_on(device);
        #if ( ! CTM201_USING_POWER_CTRL)
        ctm201_reset(device);
        #endif
        rt_thread_mdelay(3000);

        /* wait ctm201 startup finish, send AT every 500ms, if receive OK, SYNC success*/
        if (at_client_obj_wait_connect(client, CTM201_WAIT_CONNECT_TIME))
        {
            result = -RT_ETIMEOUT;
            goto __exit;
        }
        
        /* check SIM card */
        for (i = 0; i < CPIN_RETRY; i++)
        {
            rt_thread_mdelay(1000);
            if (at_obj_exec_cmd(device->client, resp, "AT+NCCID?") == RT_EOK)
            {
                break;
            }
        }
        if (i == CPIN_RETRY)
        {
            LOG_E("%s device SIM card detection failed.", device->name);
            result = -RT_ERROR;
            goto __exit;
        }
        
        /* disable PSM mode  */
        if (at_obj_exec_cmd(device->client, resp, "AT+CPSMS=0") != RT_EOK)
        {
            result = -RT_ERROR;
            goto __exit;
        }

        /* disable eDRX mode  */
        if (at_obj_exec_cmd(device->client, resp, "AT+CEDRXS=0,5") != RT_EOK)
        {
            result = -RT_ERROR;
            goto __exit;
        }

        /* check signal strength */
        for (i = 0; i < CSQ_RETRY; i++)
        {
            rt_thread_mdelay(1000);
            if (at_obj_exec_cmd(device->client, resp, "AT+CSQ") == RT_EOK)
            {
                int signal_strength = 0;
                
                if (at_resp_parse_line_args_by_kw(resp, "+CSQ:", "+CSQ: %d", &signal_strength) > 0)
                {
                    if ((signal_strength != 99) && (signal_strength != 0))
                    {
                        LOG_D("%s device signal strength: %d", device->name, signal_strength);
                        break;
                    }
                }
            }
        }
        if (i == CSQ_RETRY)
        {
            LOG_E("%s device signal strength check failed", device->name);
            result = -RT_ERROR;
            goto __exit;
        }
                
        /* check the GPRS network is registered */
        for (i = 0; i < CGREG_RETRY; i++)
        {
            rt_thread_mdelay(1000);
            if (at_obj_exec_cmd(device->client, resp, "AT+CEREG?") == RT_EOK)
            {
                int link_stat = 0;
                
                if (at_resp_parse_line_args_by_kw(resp, "+CEREG:", "+CEREG: %*d,%d", &link_stat) > 0)
                {
                    if ((link_stat == 1) || (link_stat == 5))
                    {
                        LOG_D("%s device GPRS is registered", device->name);
                        break;
                    }
                }
            }
        }
        if (i == CGREG_RETRY)
        {
            LOG_E("%s device GPRS is register failed", device->name);
            result = -RT_ERROR;
            goto __exit;
        }
        
        /* check the GPRS network IP address */
        for (i = 0; i < IPADDR_RETRY; i++)
        {
            rt_thread_mdelay(1000);
            if (at_obj_exec_cmd(device->client, resp, "AT+CGPADDR=0") == RT_EOK)
            {
                #define IP_ADDR_SIZE_MAX    16
                char ipaddr[IP_ADDR_SIZE_MAX] = {0};
                
                /* parse response data "+CGPADDR: 1,<IP_address>" */
                if (at_resp_parse_line_args_by_kw(resp, "+CGPADDR:", "+CGPADDR: %*d,%s", ipaddr) > 0)
                {
                    LOG_D("%s device IP address: %s", device->name, ipaddr);
                    break;
                }
            }
        }
        if (i == IPADDR_RETRY)
        {
            LOG_E("%s device GPRS is get IP address failed", device->name);
            result = -RT_ERROR;
            goto __exit;
        }
        
        /* initialize successfully  */
        result = RT_EOK;
        break;

    __exit:
        if (result != RT_EOK)
        {
            /* power off the ctm201 device */
            ctm201_power_off(device);
            rt_thread_mdelay(2000);

            LOG_I("%s device initialize retry...", device->name);
        }
    }

    if (resp)
    {
        at_delete_resp(resp);
    }

    if (result == RT_EOK)
    {
        /* set network interface device status and address information */
        ctm201_netdev_set_info(device->netdev);
        /* check and create link staus sync thread  */
        if (rt_thread_find(device->netdev->name) == RT_NULL)
        {
            ctm201_netdev_check_link_status(device->netdev);
        }

        LOG_I("%s device network initialize success.", device->name);
    }
    else
    {
        LOG_E("%s device network initialize failed(%d).", device->name, result);
    }
}

/* ctm201 device network initialize */
static int ctm201_net_init(struct at_device *device)
{
#ifdef AT_DEVICE_CTM201_INIT_ASYN
    rt_thread_t tid;

    tid = rt_thread_create("ctm201_net", ctm201_init_thread_entry, (void *)device,
                           CTM201_THREAD_STACK_SIZE, CTM201_THREAD_PRIORITY, 20);
    if (tid)
    {
        rt_thread_startup(tid);
    }
    else
    {
        LOG_E("create %s device init thread failed.", device->name);
        return -RT_ERROR;
    }
#else
    ctm201_init_thread_entry(device);
#endif /* AT_DEVICE_CTM201_INIT_ASYN */

    return RT_EOK;
}

static int ctm201_init(struct at_device *device)
{
    struct at_device_ctm201 *ctm201 = RT_NULL;
    struct serial_configure cfg = RT_SERIAL_CONFIG_DEFAULT;

    RT_ASSERT(device);

    ctm201 = (struct at_device_ctm201 *) device->user_data;
    ctm201->power_status = RT_FALSE;//default power is off.
    ctm201->sleep_status = RT_FALSE;//default sleep is disabled.

    /* initialize AT client */
    at_client_init(ctm201->client_name, ctm201->recv_line_num);

    device->client = at_client_get(ctm201->client_name);
    if (device->client == RT_NULL)
    {
        LOG_E("get AT client(%s) failed.", ctm201->client_name);
        return -RT_ERROR;
    }

    cfg.baud_rate = 9600;
    device->client->device->ops->control(device->client->device, RT_DEVICE_CTRL_CONFIG, &cfg);

    /* register URC data execution function  */
#ifdef AT_USING_SOCKET
    ctm201_socket_init(device);
#endif

    /* add ctm201 device to the netdev list */
    device->netdev = ctm201_netdev_add(ctm201->device_name);
    if (device->netdev == RT_NULL)
    {
        LOG_E("add netdev(%s) failed.", ctm201->device_name);
        return -RT_ERROR;
    }

    /* initialize ctm201 pin configuration */
    #if CTM201_USING_POWER_CTRL
    if (ctm201->power_pin != -1)
    {
        rt_pin_write(ctm201->power_pin, PIN_LOW);
        rt_pin_mode(ctm201->power_pin, PIN_MODE_OUTPUT);
    }
    #endif

    /* initialize ctm201 device network */
    return ctm201_netdev_set_up(device->netdev);
}

static int ctm201_deinit(struct at_device *device)
{
    RT_ASSERT(device);
    
    return ctm201_netdev_set_down(device->netdev);
}

static int ctm201_control(struct at_device *device, int cmd, void *arg)
{
    int result = -RT_ERROR;

    RT_ASSERT(device);

    switch (cmd)
    {
    case AT_DEVICE_CTRL_SLEEP:
        result = ctm201_sleep(device);
        break;
    case AT_DEVICE_CTRL_WAKEUP:
        result = ctm201_wakeup(device);
        break;
    case AT_DEVICE_CTRL_RESET:
        result = ctm201_reset(device);
        break;
    case AT_DEVICE_CTRL_POWER_ON:
    case AT_DEVICE_CTRL_POWER_OFF:
    case AT_DEVICE_CTRL_LOW_POWER:
    case AT_DEVICE_CTRL_NET_CONN:
    case AT_DEVICE_CTRL_NET_DISCONN:
    case AT_DEVICE_CTRL_SET_WIFI_INFO:
    case AT_DEVICE_CTRL_GET_SIGNAL:
    case AT_DEVICE_CTRL_GET_GPS:
    case AT_DEVICE_CTRL_GET_VER:
        LOG_W("not support the control command(%d).", cmd);
        break;
    default:
        LOG_E("input error control command(%d).", cmd);
        break;
    }

    return result;
}

const struct at_device_ops ctm201_device_ops =
{
    ctm201_init,
    ctm201_deinit,
    ctm201_control,
};

static int ctm201_device_class_register(void)
{
    struct at_device_class *class = RT_NULL;

    class = (struct at_device_class *) rt_calloc(1, sizeof(struct at_device_class));
    if (class == RT_NULL)
    {
        LOG_E("no memory for device class create.");
        return -RT_ENOMEM;
    }

    /* fill ctm201 device class object */
#ifdef AT_USING_SOCKET
    ctm201_socket_class_register(class);
#endif
    class->device_ops = &ctm201_device_ops;

    return at_device_class_register(class, AT_DEVICE_CLASS_CTM201);
}
INIT_DEVICE_EXPORT(ctm201_device_class_register);

#endif /* AT_DEVICE_USING_CTM201 */

