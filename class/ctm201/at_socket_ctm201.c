/*
 * File      : at_socket_ctm201.c
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

#define LOG_TAG                        "at.skt.ctm201"
#include <at_log.h>

#if defined(AT_DEVICE_USING_CTM201) && defined(AT_USING_SOCKET)

#define CTM201_MODULE_SEND_MAX_SIZE       1358

typedef struct{
    int sock;
    int port;
    char ip[16];
}ctm201_socket_data_t;

static ctm201_socket_data_t ctm201_socket_datas[AT_DEVICE_CTM201_SOCKETS_NUM] = {0};

static at_evt_cb_t at_evt_cb_set[] = {
        [AT_SOCKET_EVT_RECV] = NULL,
        [AT_SOCKET_EVT_CLOSED] = NULL,
};

static int ctm201_get_socket_idx(int sock)
{
    int i;
    
    if (sock < 0)
    {
        return(-1);
    }
    
    for (i=0; i<AT_DEVICE_CTM201_SOCKETS_NUM; i++)
    {
        if (ctm201_socket_datas[i].sock == sock)
            return(i);
    }

    return(-1);
}

/**
 * close socket by AT commands.
 *
 * @param current socket
 *
 * @return  0: close socket success
 *         -1: send AT commands error
 *         -2: wait socket event timeout
 *         -5: no memory
 */
static int ctm201_socket_close(struct at_socket *socket)
{
    int result = RT_EOK;
    at_response_t resp = RT_NULL;
    int device_socket = (int) socket->user_data;
    struct at_device *device = (struct at_device *) socket->device;

    if (ctm201_socket_datas[device_socket].sock == -1)
    {
        return RT_EOK;
    }
    
    resp = at_create_resp(64, 0, rt_tick_from_millisecond(300));
    if (resp == RT_NULL)
    {
        LOG_E("no memory for resp create.");
        return -RT_ENOMEM;
    }

    result = at_obj_exec_cmd(device->client, resp, "AT+NSOCL=%d", ctm201_socket_datas[device_socket].sock);
    ctm201_socket_datas[device_socket].sock = -1;
    
    at_delete_resp(resp);

    return result;
}

/**
 * create TCP/UDP client or server connect by AT commands.
 *
 * @param socket current socket
 * @param ip server or client IP address
 * @param port server or client port
 * @param type connect socket type(tcp, udp)
 * @param is_client connection is client
 *
 * @return   0: connect success
 *          -1: connect failed, send commands error or type error
 *          -2: wait socket event timeout
 *          -5: no memory
 */
static int ctm201_socket_connect(struct at_socket *socket, char *ip, int32_t port,
    enum at_socket_type type, rt_bool_t is_client)
{
    #define CONN_RESP_SIZE  128
    
    char *type_str = RT_NULL;
    int local_port;
    int result = RT_EOK;
    at_response_t resp = RT_NULL;
    int device_socket = (int) socket->user_data;
    struct at_device *device = (struct at_device *) socket->device;
    int sock = -1;

    RT_ASSERT(ip);
    RT_ASSERT(port >= 0);

    if ( ! is_client)
    {
        return -RT_ERROR;
    }

    switch(type)
    {
        case AT_SOCKET_TCP:
            type_str = "STREAM,6";
            break;
        case AT_SOCKET_UDP:
            type_str = "DGRAM,17";
            break;
        default:
            LOG_E("%s device socket(%d)  connect type error.", device->name, device_socket);
            return -RT_ERROR;
    }

    resp = at_create_resp(CONN_RESP_SIZE, 0, rt_tick_from_millisecond(300));
    if (resp == RT_NULL)
    {
        LOG_E("no memory for resp create.");
        return -RT_ENOMEM;
    }

    if (ctm201_socket_datas[device_socket].sock != -1)
    {
        at_obj_exec_cmd(device->client, resp, "AT+NSOCL=%d", ctm201_socket_datas[device_socket].sock);
        ctm201_socket_datas[device_socket].sock = -1;
    }
    
    local_port = 1024+device_socket;
    if (at_obj_exec_cmd(device->client, resp, "AT+NSOCR=%s,%d,1", type_str, local_port) < 0)
    {
        result = -RT_ERROR;
        goto __exit;
    }

    if (at_resp_parse_line_args(resp, 2, "%d", &sock) <= 0)
    {
        result = -RT_ERROR;
        goto __exit;
    }

    if(type == AT_SOCKET_TCP)
    {
        at_resp_set_info(resp, CONN_RESP_SIZE, 0, (15*RT_TICK_PER_SECOND));
        if (at_obj_exec_cmd(device->client, resp, "AT+NSOCO=%d,%s,%d", sock, ip, port) < 0)
        {
            at_resp_set_info(resp, CONN_RESP_SIZE, 0, rt_tick_from_millisecond(300));
            at_obj_exec_cmd(device->client, resp, "AT+NSOCL=%d", sock);
            result = -RT_ERROR;
            goto __exit;
        }
    }
    
    ctm201_socket_datas[device_socket].sock = sock;
    ctm201_socket_datas[device_socket].port = port;
    rt_strncpy(ctm201_socket_datas[device_socket].ip, ip, sizeof(ctm201_socket_datas[0].ip));
    
__exit:
    if (resp)
    {
        at_delete_resp(resp);
    }

    return result;
}

/**
 * send data to server or client by AT commands.
 *
 * @param socket current socket
 * @param buff send buffer
 * @param bfsz send buffer size
 * @param type connect socket type(tcp, udp)
 *
 * @return >=0: the size of send success
 *          -1: send AT commands error or send data error
 *          -2: waited socket event timeout
 *          -5: no memory
 */
static int ctm201_socket_send(struct at_socket *socket, const char *buff, size_t bfsz, enum at_socket_type type)
{
    #define SEND_RESP_SIZE      128
    
    int result = 0;
    int i=0;
    size_t cur_pkt_size = 0, sent_size = 0;
    at_response_t resp = RT_NULL;
    char *hex_str = RT_NULL;
    int device_socket = (int) socket->user_data;
    struct at_device *device = (struct at_device *) socket->device;
    rt_mutex_t lock = device->client->lock;

    RT_ASSERT(buff);


    resp = at_create_resp(SEND_RESP_SIZE, 0, 5*RT_TICK_PER_SECOND);
    if (resp == RT_NULL)
    {
        LOG_E("no memory for resp create.");
        return -RT_ENOMEM;
    }

    hex_str = rt_calloc(1, 2*(CTM201_MODULE_SEND_MAX_SIZE+1));
    if (hex_str == RT_NULL)
    {
        at_delete_resp(resp);
        LOG_E("no memory for resp create.");
        return -RT_ENOMEM;
    }

    rt_mutex_take(lock, RT_WAITING_FOREVER);

    while (sent_size < bfsz)
    {
        if (bfsz - sent_size < CTM201_MODULE_SEND_MAX_SIZE)
        {
            cur_pkt_size = bfsz - sent_size;
        }
        else
        {
            cur_pkt_size = CTM201_MODULE_SEND_MAX_SIZE;
        }

        for (i=0; i<cur_pkt_size; i++)
        {
            sprintf(hex_str+i*2, "%02X", buff[sent_size+i]);
        }

        switch(type)
        {
            case AT_SOCKET_TCP:
                if (at_obj_exec_cmd(device->client, resp, "AT+NSOSD=%d,%d,%s", ctm201_socket_datas[device_socket].sock, (int)cur_pkt_size, hex_str) < 0)
                {
                    result = -RT_ERROR;
                    goto __exit;
                }
                break;
            case AT_SOCKET_UDP:
                if (at_obj_exec_cmd(device->client, resp, "AT+NSOST=%d,%s,%d,%d,%s", ctm201_socket_datas[device_socket].sock, ctm201_socket_datas[device_socket].ip,
                                    ctm201_socket_datas[device_socket].port, (int)cur_pkt_size, hex_str) < 0)
                {
                    result = -RT_ERROR;
                    goto __exit;
                }
                break;
            default:
                LOG_E("%s device socket(%d)  connect type error.", device->name, device_socket);
                result = -RT_ERROR;
                goto __exit;
        }
        
        //rt_thread_mdelay(10);//delay at least 10 ms
        
        sent_size += cur_pkt_size;
    }

__exit:

    rt_mutex_release(lock);

    rt_free(hex_str);

    if (resp)
    {
        at_delete_resp(resp);
    }

    return result > 0 ? sent_size : result;
}

/**
 * domain resolve by AT commands.
 *
 * @param name domain name
 * @param ip parsed IP address, it's length must be 16
 *
 * @return  0: domain resolve success
 *         -1: send AT commands error or response error
 *         -2: wait socket event timeout
 *         -5: no memory
 */
static int ctm201_domain_resolve(const char *name, char ip[16])
{
    int result;
    at_response_t resp = RT_NULL;
    struct at_device *device = RT_NULL;

    RT_ASSERT(name);
    RT_ASSERT(ip);

    device = at_device_get_first_initialized();
    if (device == RT_NULL)
    {
        LOG_E("get first init device failed.");
        return -RT_ERROR;
    }

    resp = at_create_resp(128, 4, (15 * RT_TICK_PER_SECOND));
    if (!resp)
    {
        LOG_E("no memory for resp create.");
        return -RT_ENOMEM;
    }

    result = at_obj_exec_cmd(device->client, resp, "AT+MDNS=0,%s", name);
    if (result != RT_EOK)
    {
        LOG_E("%s device \"AT+MDNS=0,%s\" cmd error.", device->name, name);
        goto __exit;
    }

    if (at_resp_parse_line_args_by_kw(resp, "+MDNS:", "+MDNS:%s\r", ip) <= 0)
    {
        LOG_E("%s device prase \"AT+MDNS=0,%s\" cmd error.", device->name, name);
        result = -RT_ERROR;
        goto __exit;
    }
    
    ip[15] = 0;
    if (rt_strlen(ip) < 8)
    {
        result = -RT_ERROR;
    }
    else
    {
        result = RT_EOK;
    }

 __exit:
    if (resp)
    {
        at_delete_resp(resp);
    }

    return result;
}

/**
 * set AT socket event notice callback
 *
 * @param event notice event
 * @param cb notice callback
 */
static void ctm201_socket_set_event_cb(at_socket_evt_t event, at_evt_cb_t cb)
{
    if (event < sizeof(at_evt_cb_set) / sizeof(at_evt_cb_set[1]))
    {
        at_evt_cb_set[event] = cb;
    }
}

static void urc_close_func(struct at_client *client, const char *data, rt_size_t size)
{
    int sock = -1;
    int device_socket = 0;
    struct at_socket *socket = RT_NULL;
    struct at_device *device = RT_NULL;
    char *client_name = client->device->parent.name;

    RT_ASSERT(data && size);

    device = at_device_get_by_name(AT_DEVICE_NAMETYPE_CLIENT, client_name);
    if (device == RT_NULL)
    {
        LOG_E("get device(%s) failed.", client_name);
        return;
    }

    sscanf(data, "+NSOCLI:%d", &sock);
    
    device_socket = ctm201_get_socket_idx(sock);
    if (device_socket < 0)
    {
        return;
    }
    
    /* get at socket object by device socket descriptor */
    socket = &(device->sockets[device_socket]);

    /* notice the socket is disconnect by remote */
    if (at_evt_cb_set[AT_SOCKET_EVT_CLOSED])
    {
        at_evt_cb_set[AT_SOCKET_EVT_CLOSED](socket, AT_SOCKET_EVT_CLOSED, NULL, 0);
    }
}

static void urc_recv_func(struct at_client *client, const char *data, rt_size_t size)
{
    int sock = -1;
    int device_socket = -1;
    int i,t;
    rt_size_t bfsz = 0, read_size;
    char *recv_buf = RT_NULL, temp[20] = {0};
    struct at_socket *socket = RT_NULL;
    struct at_device *device = RT_NULL;
    char *client_name = client->device->parent.name;

    RT_ASSERT(data && size);

    device = at_device_get_by_name(AT_DEVICE_NAMETYPE_CLIENT, client_name);
    if (device == RT_NULL)
    {
        LOG_E("get device(%s) failed.", client_name);
        return;
    }

    sscanf(data, "+NSONMI:%d,%d", &sock, &bfsz);
    device_socket = ctm201_get_socket_idx(sock);
    if (device_socket < 0)
    {
        return;
    }
    if(bfsz == 0)
    {
        return;
    }
    
    at_obj_exec_cmd(client, RT_NULL, "AT+NSORF=%d,%d", sock, bfsz);
    
    recv_buf = (char *) rt_calloc(1, bfsz);
    if (recv_buf == RT_NULL)
    {
        LOG_E("no memory for URC receive buffer(%d).", bfsz);
        /* read and clean the coming command resp */
        while (1)
        {
            read_size = at_client_obj_recv(client, temp, sizeof(temp), 10);
            if (read_size < sizeof(temp))
            {
                break;
            }
        }
        return;
    }
    
    //read socket id
    i = 0;
    while (1)
    {
        read_size = at_client_obj_recv(client, temp+i, 1, 20);
        if (read_size < 1)
        {
            LOG_E("%s device read receive socket id failed.", device->name);
            rt_free(recv_buf);
            return;
        }
        if (temp[i] == ',')
        {
            temp[i] = 0;
            break;
        }
        i++;
    }
    
    //read ip address
    i = 0;
    while (1)
    {
        read_size = at_client_obj_recv(client, temp+i, 1, 10);
        if (read_size < 1)
        {
            LOG_E("%s device read receive ip address failed.", device->name);
            rt_free(recv_buf);
            return;
        }
        if (temp[i] == ',')
        {
            temp[i] = 0;
            break;
        }
        i++;
    }

    //read port
    i = 0;
    while (1)
    {
        read_size = at_client_obj_recv(client, temp+i, 1, 10);
        if (read_size < 1)
        {
            LOG_E("%s device read receive port failed.", device->name);
            rt_free(recv_buf);
            return;
        }
        if (temp[i] == ',')
        {
            temp[i] = 0;
            break;
        }
        i++;
    }

    //read data length
    i = 0;
    while (1)
    {
        read_size = at_client_obj_recv(client, temp+i, 1, 10);
        if (read_size < 1)
        {
            LOG_E("%s device read receive data length failed.", device->name);
            rt_free(recv_buf);
            return;
        }
        if (temp[i] == ',')
        {
            temp[i] = 0;
            break;
        }
        i++;
    }

    //read datas
    for (i=0; i<bfsz; i++)
    {
        read_size = at_client_obj_recv(client, temp, 2, 10);
        if (read_size < 2)
        {
            LOG_E("%s device read receive datas failed.", device->name);
            rt_free(recv_buf);
            return;
        }
        temp[2] = 0;
        sscanf(temp, "%02X", &t);
        recv_buf[i] = t;
    }

    //read and clean other
    while (1)
    {
        read_size = at_client_obj_recv(client, temp, 1, 10);
        if (read_size < 1)
        {
            break;
        }
        if (temp[0] == '\n')
        {
            break;
        }
    }

    /* get at socket object by device socket descriptor */
    socket = &(device->sockets[device_socket]);

    /* notice the receive buffer and buffer size */
    if (at_evt_cb_set[AT_SOCKET_EVT_RECV])
    {
        at_evt_cb_set[AT_SOCKET_EVT_RECV](socket, AT_SOCKET_EVT_RECV, recv_buf, bfsz);
    }
}

static const struct at_urc urc_table[] =
{
    {"+NSOCLI:",    "\r\n",                 urc_close_func},
    {"+NSONMI:",    "\r\n",                 urc_recv_func},
};

static const struct at_socket_ops ctm201_socket_ops =
{
    ctm201_socket_connect,
    ctm201_socket_close,
    ctm201_socket_send,
    ctm201_domain_resolve,
    ctm201_socket_set_event_cb,
};

int ctm201_socket_init(struct at_device *device)
{
    int i;
    
    RT_ASSERT(device);
    
    rt_memset(ctm201_socket_datas, 0, sizeof(ctm201_socket_datas));
    for(i=0; i<AT_DEVICE_CTM201_SOCKETS_NUM; i++)
    {
        ctm201_socket_datas[i].sock = -1;
    }

    /* register URC data execution function  */
    at_obj_set_urc_table(device->client, urc_table, sizeof(urc_table) / sizeof(urc_table[0]));

    return RT_EOK;
}

int ctm201_socket_class_register(struct at_device_class *class)
{
    RT_ASSERT(class);

    class->socket_num = AT_DEVICE_CTM201_SOCKETS_NUM;
    class->socket_ops = &ctm201_socket_ops;

    return RT_EOK;
}

#endif /* AT_DEVICE_USING_CTM201 && AT_USING_SOCKET */

