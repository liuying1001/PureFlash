/**
 * Copyright (C), 2019.
 *
 * @file
 * This file defines toe server initialization and release func.
 */
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <string>

#include "cmdopt.h"
#include "s5_utils.h"
#include "afs_server.h"
#include "s5errno.h"
#include "afs_cluster.h"
#include "s5_app_ctx.h"

using namespace std;
int init_restful_server();

S5AfsAppContext app_context;

void sigroutine(int dunno)
{
	switch(dunno)
	{
		case SIGTERM:
			S5LOG_INFO("Recieve signal SIGTERM.");
			exit(1);

		case SIGINT:
			S5LOG_INFO("Recieve signal SIGINT.");
			exit(1);
	}
	return;
}



static void printUsage()
{
	S5LOG_ERROR("Usage: s5afs -c <s5daemon_conf_file> \n");
}



int main(int argc, char *argv[])
{
	int rc = -1;
	const char*	s5daemon_conf = NULL;

	S5LOG_INFO("S5afs start...");
	if (argc < 3)
	{
		printUsage();
		rc = -EINVAL;
		S5LOG_ERROR("Failed: param is not enough to start argc(%d).", argc);
		return rc;
	}

	g_app_ctx = &app_context;
	opt_initialize(argc, (const char**)argv);
	while(opt_error_code() == 0 && opt_has_next())
	{
		const char* name = opt_next();
		if(strcmp(name, "c") == 0)
		{
			s5daemon_conf = opt_value();
		}
		else
		{
			S5LOG_ERROR("Failed: Invalid argument \"%s\"", name);
			printUsage();
			opt_uninitialize();
			return -EINVAL;;
		}
	}
	if(opt_error_code() != 0)
	{
		S5LOG_ERROR("Failed: error_code=%s", opt_error_message());
		printUsage();
		opt_uninitialize();
		return -EINVAL;
	}
	opt_uninitialize();

	conf_file_t fp = NULL;
	if(s5daemon_conf == NULL)
		s5daemon_conf = "/etc/pureflash/s5afs.conf";
	fp = conf_open(s5daemon_conf);
	if(!fp)
	{
		S5LOG_FATAL("Failed to find S5afs conf(%s)", s5daemon_conf);
		return -S5_CONF_ERR;
	}
	app_context.conf = fp;
	const char *zk_ip = conf_get(fp, "zookeeper", "ip", NULL, true);
	if(!zk_ip)
	{
		S5LOG_FATAL("Failed to find key(zookeeper:ip) in s5afs conf(%s).", s5daemon_conf);
		return -S5_CONF_ERR;
	}


	const char *this_mngt_ip = conf_get(fp, "afs", "mngt_ip", NULL, true);
	if (!this_mngt_ip)
	{
		S5LOG_FATAL("Failed to find key(conductor:mngt_ip) in s5afs conf(%s).", s5daemon_conf);
		return -S5_CONF_ERR;
	}
	const char *cluster_name = conf_get(fp, "cluster", "name", NULL, true);
	if (!cluster_name)
	{
		S5LOG_FATAL("Failed to find key(conductor:mngt_ip) in s5afs conf(%s).", s5daemon_conf);
		return -S5_CONF_ERR;
	}
	app_context.mngt_ip = this_mngt_ip;
	rc = init_cluster(zk_ip, cluster_name);
	if (rc)
	{
		S5LOG_ERROR("Failed to connect zookeeper");
		return rc;
	}

	int store_id = conf_get_int(fp, "afs", "id", 0, TRUE);
	if(store_id == 0)
	{
		S5LOG_FATAL("afs.id not defined in conf file");
	}
	app_context.store_id = store_id;
	S5LOG_INFO("Register store to ZK.");
	rc = register_store_node(store_id, this_mngt_ip);
	if (rc)
	{
		S5LOG_ERROR("Failed to register store");
		return rc;
	}

	int i=0;
	for(i=0;i<MAX_TRAY_COUNT;i++)
	{
		string name = format_string("tray.%d", i);
		const char* devname = conf_get(fp, name.c_str(), "dev", NULL, false);
		if(devname == NULL)
			break;
		auto s = new S5FlashStore();
		rc = s->init(devname);
		if(rc)
		{
			S5LOG_ERROR("Failed init tray:%s, rc:%d", devname, rc);
			continue;
		}
		else
		{
			app_context.trays.push_back(s);
		}
		register_tray(store_id, s->head.uuid, s->tray_name, s->head.tray_capacity);

	}
	for (i = 0; i < MAX_PORT_COUNT; i++)
	{
		string name = format_string("port.%d", i);
		const char* ip = conf_get(fp, name.c_str(), "ip", NULL, false);
		if (ip == NULL)
			break;
		int purpose = conf_get_int(fp, name.c_str(), "purpose", DATA_PORT, false);
		rc = register_port(store_id, ip, purpose);
		if(rc) {
			S5LOG_ERROR("Failed register port:%s, rc:%d", ip, rc);
			continue;
		}

	}
	app_context.tcp_server=new S5TcpServer();
	rc = app_context.tcp_server->init();
	if(rc)
	{
		S5LOG_ERROR("Failed to init tcp server:%d", rc);
		return rc;
	}
	set_store_node_state(store_id, NS_OK, TRUE);
	signal(SIGTERM, sigroutine);
	signal(SIGINT, sigroutine);
	init_restful_server();
	while(sleep(1) == 0);

	S5LOG_INFO("toe_daemon exit.");
	return rc;
}

int S5AfsAppContext::get_ssd_index(std::string ssd_uuid)
{
	for(int i=0;i<trays.size();i++)
	{
		if (ssd_uuid == (char*)trays[i]->head.uuid)
			return i;
	}
	return -1;
}

S5AfsAppContext::S5AfsAppContext()
{
	pthread_mutex_init(&lock, NULL);
}

S5Volume* S5AfsAppContext::get_opened_volume(uint64_t vol_id)
{
	pthread_mutex_lock(&app_context.lock);
	auto pos = opened_volumes.find(vol_id);
	if (pos == opened_volumes.end())
		return NULL;
	return pos->second;
}