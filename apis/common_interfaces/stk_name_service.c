
extern "C" {
#include "../../include/stk_name_service.h"
}

void stk_name_from_name_info(stk_name_info_t *name_info, char **name) {
	*name = name_info->name;
}

void stk_rip_from_name_info(stk_name_info_t *name_info,int idx,char *str)
{
	inet_ntop(AF_INET, &name_info->ip[idx].sockaddr.sin_addr, str, 16);
}

short stk_rip_port_from_name_info(stk_name_info_t *name_info,int idx)
{
	return name_info->ip[idx].sockaddr.sin_port;
}

stk_name_ip_t stk_get_ip_from_name_info(stk_name_info_t *name_info,int idx)
{
	return name_info->ip[idx];
}

stk_sequence_t *stk_sequence_from_name_info(stk_name_info_t *name_info) {
	return name_info->meta_data;
}

void stk_name_info_cb(stk_name_info_t *name_info,int name_count,void *server_info,void *app_info,stk_name_info_cb_type cb_type)
{
}

stk_ret stk_register_name_nocb(stk_name_service_t *named,char *name, int linger, int expiration_ms, void *app_info, stk_options_t *options)
{
	return stk_register_name(named,name, linger, expiration_ms, stk_name_info_cb, app_info, options);
}
