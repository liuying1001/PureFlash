#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pf_main.h>
#include "pf_cluster.h"
#include "zookeeper.h"
#include "pf_log.h"

#define ZK_TIMEOUT_MSEC 3000

int init_cluster(const char* zk_ip_port, const char* cluster_name)
{
    int rc = app_context.zk_client.init(zk_ip_port, ZK_TIMEOUT_MSEC, cluster_name);
	if (rc)
	{
		S5LOG_ERROR("Failed to connect zk, errno:%d", rc);
		return rc;
	}
	return 0;
}

/*
 * set node's content, create node if node not exists.
 * on error, an error message will be logged.
 * @param node name of node to update or create
 * @param value buffer to node content, pass NULL if node has no value, or clear value of node
 * @param val_len length of value, in byte. pass -1 if value is NULL.
 *
 * @return ZOK on success, negative value on error
 * @retval ZNONODE the parent node does not exist.
 * @retval ZNOAUTH the client does not have permission.
 * @retval ZNOCHILDRENFOREPHEMERALS cannot create children of ephemeral nodes.
 * @retval ZBADARGUMENTS - invalid input parameters
 * @retval ZINVALIDSTATE - zhandle state is either ZOO_SESSION_EXPIRED_STATE or ZOO_AUTH_FAILED_STATE
 * @retval ZMARSHALLINGERROR - failed to marshall a request; possibly, out of memory
 *
 *
 */
static int zk_update(const char* node, const char* value, int val_len)
{
	int rc = zoo_create(app_context.zk_client.zkhandle, node, NULL, -1, &ZOO_OPEN_ACL_UNSAFE, 0, NULL, 0);
	if (rc != ZOK && rc != ZNODEEXISTS)
	{
		S5LOG_ERROR("Failed to create zookeeper node %s rc:%d", node, rc);
		return rc;
	}
	rc = zoo_set(app_context.zk_client.zkhandle, node, value, value ? val_len : -1, -1);
	if (rc != ZOK)
	{
		S5LOG_ERROR("Failed to update zookeeper node value node:%s rc:%d", node, rc);
		return rc;
	}
	return ZOK;
}

int set_store_node_state(int store_id, const char* state, BOOL alive)
{
	char zk_node_name[64];
	int rc;
	snprintf(zk_node_name, sizeof(zk_node_name), "/s5/stores/%d/state", store_id);
	if ((rc = zk_update(zk_node_name, state, (int)strlen(state))) != ZOK)
		return rc;

	snprintf(zk_node_name, sizeof(zk_node_name), "/s5/stores/%d/alive", store_id);
	if(alive)
	{
		rc = zoo_create(app_context.zk_client.zkhandle, zk_node_name, NULL, -1, &ZOO_OPEN_ACL_UNSAFE, ZOO_EPHEMERAL, NULL, 0);
		if (rc != ZOK && rc != ZNODEEXISTS)
		{
			S5LOG_ERROR("Failed to create zookeeper node %s rc:%d", zk_node_name, rc);
			return rc;
		}
	}
	else
	{
		rc = zoo_delete(app_context.zk_client.zkhandle, zk_node_name, -1);
		return rc;
	}
	return ZOK;

}
int register_store_node(int store_id, const char* mngt_ip)
{
	char zk_node_name[64];
	int rc;
	if ((rc = zk_update("/s5", NULL, 0)) != ZOK)
		return rc;
	if ((rc = zk_update("/s5/stores", NULL, 0)) != ZOK)
		return rc;

	snprintf(zk_node_name, sizeof(zk_node_name), "/s5/stores/%d", store_id);
	if ((rc = zk_update(zk_node_name, NULL, 0)) != ZOK)
		return rc;
	snprintf(zk_node_name, sizeof(zk_node_name), "/s5/stores/%d/mngt_ip", store_id);
	if ((rc = zk_update(zk_node_name, mngt_ip, strlen(mngt_ip))) != ZOK)
		return rc;


	snprintf(zk_node_name, sizeof(zk_node_name), "/s5/stores/%d/trays", store_id);
	if ((rc = zk_update(zk_node_name, NULL, 0)) != ZOK)
		return rc;
	snprintf(zk_node_name, sizeof(zk_node_name), "/s5/stores/%d/ports", store_id);
	if ((rc = zk_update(zk_node_name, NULL, 0)) != ZOK)
		return rc;
	snprintf(zk_node_name, sizeof(zk_node_name), "/s5/stores/%d/rep_ports", store_id);
	if ((rc = zk_update(zk_node_name, NULL, 0)) != ZOK)
		return rc;
	return 0;
}

int set_tray_state(int store_id, const uuid_t uuid, const char* state, BOOL online)
{
	char zk_node_name[128];
	char uuid_str[64];
	int rc;
	uuid_unparse(uuid, uuid_str);
	snprintf(zk_node_name, sizeof(zk_node_name), "/s5/stores/%d/trays/%s/state", store_id, uuid_str);
	if ((rc = zk_update(zk_node_name, NULL, 0)) != ZOK)
		return rc;
	snprintf(zk_node_name, sizeof(zk_node_name), "/s5/stores/%d/trays/%s/online", store_id, uuid_str);
	if (online)
	{
		rc = zoo_create(app_context.zk_client.zkhandle, zk_node_name, NULL, -1, &ZOO_OPEN_ACL_UNSAFE, ZOO_EPHEMERAL, NULL, 0);
		if (rc != ZOK && rc != ZNODEEXISTS)
		{
			S5LOG_ERROR("Failed to create zookeeper node %s rc:%d", zk_node_name, rc);
			return rc;
		}
	}
	else
	{
		rc = zoo_delete(app_context.zk_client.zkhandle, zk_node_name, -1);
		return rc;
	}
	return ZOK;
}

int register_tray(int store_id, const uuid_t uuid, const char* devname, int64_t capacity)
{
	char zk_node_name[128];
	char value_buf[128];
	char uuid_str[64];
	int rc;
	uuid_unparse(uuid, uuid_str);
	snprintf(zk_node_name, sizeof(zk_node_name), "/s5/stores/%d/trays/%s", store_id, uuid_str);
	if ((rc = zk_update(zk_node_name, NULL, 0)) != ZOK)
		return rc;

	snprintf(zk_node_name, sizeof(zk_node_name), "/s5/stores/%d/trays/%s/devname", store_id, uuid_str);
	if ((rc = zk_update(zk_node_name, devname, (int)strlen(devname))) != ZOK)
		return rc;


	snprintf(zk_node_name, sizeof(zk_node_name), "/s5/stores/%d/trays/%s/capacity", store_id, uuid_str);
	int len = snprintf(value_buf, sizeof(value_buf), "%ld", capacity);
	if ((rc = zk_update(zk_node_name, value_buf, (int)len)) != ZOK)
		return rc;
	set_tray_state(store_id, uuid, "OK", true);
	return 0;
}

int register_port(int store_id, const char* ip, int purpose)
{
	char zk_node_name[128];
	char value_buf[128];
	int rc;
	int n = snprintf(zk_node_name, sizeof(zk_node_name), "/s5/stores/%d/%s/%s", store_id, 
			purpose == DATA_PORT ? "ports" : "rep_ports", ip);
	if(n >= sizeof(zk_node_name))
	{	
		return -ENAMETOOLONG;
	}
	if ((rc = zk_update(zk_node_name, NULL, 0)) != ZOK)
		return rc;

	return 0;
}
