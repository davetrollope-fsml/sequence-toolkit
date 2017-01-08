#include "stkhttpd.h"
#include "stk_httpcontent.h"
#include "stk_smartbeat_api.h"
#include "stk_common.h"
#include "stk_internal.h"
#include "stk_sync_api.h"
#include "PLists.h"
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

extern stk_mutex_t *service_list_lock;
extern stk_mutex_t *service_history_lock;
extern List *service_list;
extern List *service_history; /* Removed Services */
extern int service_hist_max,service_hist_size;

stk_smartbeat_t daemon_smb;
char content[10240000];

int stk_url_cat(char *file,int content_length)
{
	FILE *f = fopen(file,"r");
	char localbuf[2048];

	if(!f) return content_length;

	while(!feof(f)) {
		int read = fread(&localbuf,1,sizeof(localbuf),f);
		memcpy(&content[content_length],localbuf,read);
		content_length += read;
	}

	fclose(f);

	return content_length;
}

#define STK_ADD_CONTENT(_buf,_len,...) \
		_len += snprintf(&_buf[_len], sizeof(_buf) - _len, __VA_ARGS__);

int stk_url_header()
{
	int content_length = stk_url_cat("header.phtml",0);
	if(content_length == 0)
		STK_ADD_CONTENT(content,content_length,"<HTML><BODY>");

	return content_length;
}
int stk_url_footer(int content_length)
{
	int length = stk_url_cat("footer.phtml",content_length);
	if(length == 0) {
		STK_ADD_CONTENT(content,content_length,"</BODY></HTML>");
		return content_length;
	} else
		return length;
}

int stk_url_notfound(const struct mg_request_info *request_info)
{
	int content_length = stk_url_header();
	
	content_length = stk_url_cat("banner.phtml",content_length);

	STK_ADD_CONTENT(content,content_length,
						"Sorry, the page you were looking for '%s' was not found", request_info->uri);

	content_length = stk_url_footer(content_length);
	return content_length;
}

int stk_output_svc_obj(int content_length,stk_collect_service_data_t *svcdata)
{
	STK_ADD_CONTENT(content,content_length, "svc_obj = new Object();\n");
	STK_ADD_CONTENT(content,content_length, "svc_obj.id = '%lu';", svcdata->svcinst.id);
	STK_ADD_CONTENT(content,content_length, "svc_obj.hid = '0x%lx';", svcdata->svcinst.id);
	STK_ADD_CONTENT(content,content_length, "svc_obj.type = %d;", svcdata->svcinst.type);
	STK_ADD_CONTENT(content,content_length, "svc_obj.state = %d;", svcdata->svcinst.state);
	if(svcdata->state_name)
		STK_ADD_CONTENT(content,content_length, "svc_obj.state_name = '%s';", svcdata->state_name);
	if(svcdata->svc_name)
		STK_ADD_CONTENT(content,content_length, "svc_obj.name = '%s';", svcdata->svc_name);
	if(svcdata->svc_grp_name)
		STK_ADD_CONTENT(content,content_length, "svc_obj.group_name = '%s';", svcdata->svc_grp_name);
	STK_ADD_CONTENT(content,content_length, "svc_obj.checkpoint = %lu;", svcdata->svcinst.smartbeat.checkpoint);
	STK_ADD_CONTENT(content,content_length, "svc_obj.tv_sec = %lu;", svcdata->svcinst.smartbeat.sec);
	STK_ADD_CONTENT(content,content_length, "svc_obj.tv_msec = %lu;", svcdata->svcinst.smartbeat.usec/1000);
	STK_ADD_CONTENT(content,content_length, "svc_obj.rcv_tv_sec = %lu;", svcdata->rcv_time.sec);
	STK_ADD_CONTENT(content,content_length, "svc_obj.rcv_tv_msec = %lu;", svcdata->rcv_time.usec/1000);
	{
	char str[INET_ADDRSTRLEN];
	struct sockaddr_in ipaddr;

	memcpy(&ipaddr,&svcdata->ipaddr,sizeof(ipaddr));
	/* ipaddr.sin_addr.s_addr = htonl(ipaddr.sin_addr.s_addr); not needed now? Convert to network order for inet_ntop() */

	if(inet_ntop(AF_INET, &ipaddr.sin_addr.s_addr, str, sizeof(str))) {
		STK_ADD_CONTENT(content,content_length, "svc_obj.ip = \"%s\";",str);
		STK_ADD_CONTENT(content,content_length, "svc_obj.port = %d;",((struct sockaddr_in *)&svcdata->ipaddr)->sin_port);
	}
	}
	if(svcdata->client_protocol[0])
		STK_ADD_CONTENT(content,content_length, "svc_obj.protocol = \"%s\";",svcdata->client_protocol);
	if(svcdata->displaced)
		STK_ADD_CONTENT(content,content_length, "svc_obj.displaced = 1;");
	if(svcdata->inactivity)
		STK_ADD_CONTENT(content,content_length, "svc_obj.inactivity = 1;");
	return content_length;
}

typedef enum
{
	URL_FULL_SERVICE_LIST,
	URL_GROUP_LIST,
	URL_SERVICE_NAME,
	URL_SERVICE_ID,
	URL_GROUP_NAME,
	URL_SERVICE_IP,
} stk_page_type;

char *stk_url_page_name(stk_page_type page_type)
{
	return 	page_type == URL_GROUP_LIST ? "service_groups" : 
			page_type == URL_SERVICE_NAME ? "service" : 
			page_type == URL_SERVICE_ID ? "service_id" : 
			page_type == URL_SERVICE_IP ? "service_ip" : 
			page_type == URL_GROUP_NAME ? "group" : 
			/* URL_FULL_SERVICE_LIST */ "services";
}

int stk_url_common_services(const struct mg_request_info *request_info,stk_page_type page_type, char *filter)
{
	stk_ret rc = stk_smartbeat_update_current_time(&daemon_smb);
	int content_length = stk_url_header();
	content_length = stk_url_cat("charts.phtml",content_length);
	if(filter && filter[0] == '\0') filter = NULL;

	STK_ASSERT(STKA_HTTPD,rc==STK_SUCCESS,"Get system time in stk_url_common_services");

	content_length = stk_url_cat("banner.phtml",content_length);

	STK_ADD_CONTENT(content,content_length, "<iframe frameborder=0 seamless scrolling=\"no\" allowTransparency=\"true\" id=\"data_frame\" name=\"data_frame\" width=1 height=1 src=\"/%s_data/%s\"></iframe>\n", stk_url_page_name(page_type), filter ? filter : "");
	STK_ADD_CONTENT(content,content_length,
		"<script>var services = new Array(); var services_size = 0; var svc_obj; var service_history = new Array(); var services_history_size = 0;\n");
	STK_ADD_CONTENT(content,content_length,"daemon_obj = new Object();\n");
	STK_ADD_CONTENT(content,content_length,"daemon_obj.sec = %lu;", daemon_smb.sec);
	STK_ADD_CONTENT(content,content_length,"daemon_obj.msec = %lu;\n", daemon_smb.usec/1000);
	STK_ADD_CONTENT(content,content_length,"</script>");

	STK_ADD_CONTENT(content,content_length, "<div id=\"dservices\"></div>\n");

	STK_ADD_CONTENT(content,content_length,"<script>");

	STK_ADD_CONTENT(content,content_length,"var svctimer; function show_services() { if(typeof stkloaded != undefined) { svctimer = clearInterval(svctimer); list_services('dservices'); } }\n");
	STK_ADD_CONTENT(content,content_length,"svctimer = setInterval(\"show_services();\",10);\n");

	STK_ADD_CONTENT(content,content_length,"</script>");
	content_length = stk_url_footer(content_length);
	return content_length;
}

#define stk_url_unfiltered_service_data(_array,_sz) \
				switch(page_type) { \
				case URL_FULL_SERVICE_LIST: \
					content_length = stk_output_svc_obj(content_length,svcdata); \
					STK_ADD_CONTENT(content,content_length,"\n" _array "[" _sz "++] = svc_obj;\n"); \
					break; \
				case URL_GROUP_LIST: \
					if(svcdata->svc_grp_name) { \
						content_length = stk_output_svc_obj(content_length,svcdata); \
						STK_ADD_CONTENT(content,content_length,"\n" _array "[" _sz "++] = svc_obj;\n"); \
					} \
				}

#define stk_url_filter_service_data(_array,_sz) \
				switch(page_type) { \
				case URL_SERVICE_NAME: \
					if(svcdata->svc_name && strcasecmp(svcdata->svc_name,filter) == 0) { \
						content_length = stk_output_svc_obj(content_length,svcdata); \
						STK_ADD_CONTENT(content,content_length,"\n" _array "[" _sz "++] = svc_obj;\n"); \
					} \
					break; \
				case URL_SERVICE_ID: \
					{ char id_str[64]; sprintf(id_str,"0x%lx",svcdata->svcinst.id); \
					if(strcasecmp(id_str,filter) == 0) { \
						content_length = stk_output_svc_obj(content_length,svcdata); \
						STK_ADD_CONTENT(content,content_length,"\n" _array "[" _sz "++] = svc_obj;\n"); \
					} \
					} \
					break; \
				case URL_SERVICE_IP: \
					{ \
					char str[INET_ADDRSTRLEN]; \
					struct sockaddr_in ipaddr; \
												\
					memcpy(&ipaddr,&svcdata->ipaddr,sizeof(ipaddr)); \
					ipaddr.sin_addr.s_addr = htonl(ipaddr.sin_addr.s_addr); /* Convert to network order for inet_ntop() */ \
																						\
					if(inet_ntop(AF_INET, &ipaddr.sin_addr.s_addr, str, sizeof(str))) { \
						if(strcasecmp(str,filter) == 0) { \
							content_length = stk_output_svc_obj(content_length,svcdata); \
							STK_ADD_CONTENT(content,content_length,"\n" _array "[" _sz "++] = svc_obj;\n"); \
						} \
					} \
					} \
					break; \
				case URL_GROUP_NAME: \
					if(svcdata->svc_grp_name && strcasecmp(svcdata->svc_grp_name,filter) == 0) { \
						content_length = stk_output_svc_obj(content_length,svcdata); \
						STK_ADD_CONTENT(content,content_length,"\n" _array "[" _sz "++] = svc_obj;\n"); \
					} \
					break; \
				}

int stk_url_common_service_data(const struct mg_request_info *request_info,stk_page_type page_type, char *filter)
{
	stk_ret rc = stk_smartbeat_update_current_time(&daemon_smb),lockret;
	int content_length = 0;
	if(filter && filter[0] == '\0') filter = NULL;

	STK_ASSERT(STKA_HTTPD,rc==STK_SUCCESS,"Get system time in stk_url_common_data");

	content_length = stk_url_cat("header-data.phtml",0);
	STK_ADD_CONTENT(content,content_length,
		"<script>var services = new Array(); var services_size = 0; var svc_obj; var service_history = new Array(); var services_history_size = 0;\n");

	STK_ADD_CONTENT(content,content_length,"daemon_obj = new Object();\n");
	STK_ADD_CONTENT(content,content_length,"daemon_obj.sec = %lu;", daemon_smb.sec);
	STK_ADD_CONTENT(content,content_length,"daemon_obj.msec = %lu;\n", daemon_smb.usec/1000);

	lockret = stk_mutex_lock(service_list_lock);
	STK_ASSERT(STKA_HTTPD,lockret==STK_SUCCESS,"service list lock");

	if(!IsPListEmpty(service_list)) {
		Node *on = NULL;

		for(Node *n = FirstNode(service_list); !AtListEnd(n); n = on ) {
			stk_collect_service_data_t *svcdata = (stk_collect_service_data_t *) NodeData(n);

			/* Timeout services while we walk the list - more efficient to do it here */
			on = NxtNode(n); /* Save next pointer in case n is removed */
			if(stk_timeout_service(svcdata,n) == STK_TRUE) continue; /* Timed out */

			if(filter == NULL) {
				stk_url_unfiltered_service_data("services","services_size");
			} else {
				stk_url_filter_service_data("services","services_size");
			}
		}
	}

	lockret = stk_mutex_unlock(service_list_lock);
	STK_ASSERT(STKA_HTTPD,lockret==STK_SUCCESS,"service list unlock");

	lockret = stk_mutex_lock(service_history_lock);
	STK_ASSERT(STKA_HTTPD,lockret==STK_SUCCESS,"service history lock");

	if(!IsPListEmpty(service_history)) {
		for(Node *n = FirstNode(service_history); !AtListEnd(n); n = NxtNode(n)) {
			stk_collect_service_data_t *svcdata = (stk_collect_service_data_t *) NodeData(n);

			if(filter == NULL) {
				stk_url_unfiltered_service_data("service_history","services_history_size");
			} else {
				stk_url_filter_service_data("service_history","services_history_size");
			}
		}
	}

	lockret = stk_mutex_unlock(service_history_lock);
	STK_ASSERT(STKA_HTTPD,lockret==STK_SUCCESS,"service history unlock");

	STK_ADD_CONTENT(content,content_length,"var data_loaded = true; copy_to_parent();</script>\n");

	STK_ADD_CONTENT(content,content_length,"</BODY></HTML>");
	return content_length;
}

int stk_url_service_data(const struct mg_request_info *request_info)
{
	char *filter = strchr(request_info->uri + 1,'/');
	return stk_url_common_service_data(request_info,URL_SERVICE_NAME,filter ? filter + 1 : NULL);
}

int stk_url_service(const struct mg_request_info *request_info)
{
	char *filter = strchr(request_info->uri + 1,'/');
	return stk_url_common_services(request_info,URL_SERVICE_NAME,filter ? filter + 1 : NULL);
}

int stk_url_service_id_data(const struct mg_request_info *request_info)
{
	char *filter = strchr(request_info->uri + 1,'/');
	return stk_url_common_service_data(request_info,URL_SERVICE_ID,filter ? filter + 1 : NULL);
}

int stk_url_service_id(const struct mg_request_info *request_info)
{
	char *filter = strchr(request_info->uri + 1,'/');
	return stk_url_common_services(request_info,URL_SERVICE_ID,filter ? filter + 1 : NULL);
}

int stk_url_service_ip_data(const struct mg_request_info *request_info)
{
	char *filter = strchr(request_info->uri + 1,'/');
	return stk_url_common_service_data(request_info,URL_SERVICE_IP,filter ? filter + 1 : NULL);
}

int stk_url_service_ip(const struct mg_request_info *request_info)
{
	char *filter = strchr(request_info->uri + 1,'/');
	return stk_url_common_services(request_info,URL_SERVICE_IP,filter ? filter + 1 : NULL);
}

int stk_url_group_data(const struct mg_request_info *request_info)
{
	char *filter = strchr(request_info->uri + 1,'/');
	return stk_url_common_service_data(request_info,URL_GROUP_NAME,filter ? filter + 1 : NULL);
}

int stk_url_services_data(const struct mg_request_info *request_info)
{
	return stk_url_common_service_data(request_info,URL_FULL_SERVICE_LIST,NULL);
}

int stk_url_service_groups_data(const struct mg_request_info *request_info)
{
	char *filter = strchr(request_info->uri + 1,'/');
	return stk_url_common_service_data(request_info,URL_GROUP_LIST,filter ? filter + 1 : NULL);
}

int stk_url_services(const struct mg_request_info *request_info)
{
	return stk_url_common_services(request_info,URL_FULL_SERVICE_LIST,NULL);
}

int stk_url_service_groups(const struct mg_request_info *request_info)
{
	return stk_url_common_services(request_info,URL_GROUP_LIST,NULL);
}

int stk_url_group(const struct mg_request_info *request_info)
{
	char *filter = strchr(request_info->uri + 1,'/');
	return stk_url_common_services(request_info,URL_GROUP_NAME,filter ? filter + 1 : NULL);
}

int stk_url_toplevel(const struct mg_request_info *request_info)
{
	int content_length = stk_url_header();
	content_length = stk_url_cat("banner.phtml",content_length);

	content_length = stk_url_footer(content_length);
	return content_length;
}


struct stk_url_matches urlroutes[] = {
	{ "/services_data", stk_url_services_data },
	{ "/services_data/", stk_url_services_data },
	{ "/service_groups_data", stk_url_service_groups_data },
	{ "/service_groups_data/", stk_url_service_groups_data },
	{ "/services", stk_url_services },
	{ "/services/", stk_url_services },
	{ "/service_groups", stk_url_service_groups },
	{ "/service_groups/", stk_url_service_groups },
	{ "/index.html", stk_url_toplevel },
	{ "/", stk_url_toplevel },
	{ NULL, NULL }
};
struct stk_url_prefixes urlproutes[] = {
	{ "/js/", NULL, 4 },
	{ "/images/", NULL, 8 },
	{ "/group_data/", stk_url_group_data, 12},
	{ "/group/", stk_url_group, 7 },
	{ "/service_data/", stk_url_service_data, 14},
	{ "/service/", stk_url_service, 9 },
	{ "/service_id_data/", stk_url_service_id_data, 17},
	{ "/service_id/", stk_url_service_id, 12 },
	{ "/service_ip_data/", stk_url_service_ip_data, 17},
	{ "/service_ip/", stk_url_service_ip, 12 },
	{ NULL, NULL, 0 }
};

