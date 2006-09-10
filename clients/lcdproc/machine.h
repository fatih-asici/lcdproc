
#ifndef _lcdproc_machine_h_
#define _lcdproc_machine_h_

#include "shared/LL.h"

typedef struct
{
	unsigned long total, user, system, nice, idle;
} load_type;

typedef struct
{
	char dev[256], type[64], mpoint[256];
	long bsize, blocks, bfree, files, ffree;
} mounts_type;

typedef struct
{
	int total, cache, buffers, free, shared;
} meminfo_type;

typedef struct
{
	char name[16];
	int totl;
	int number;
} procinfo_type;

/* status definitions for network interfaces */
typedef enum {
	down = 0,
	up = 1,
} IfaceStatus;

/* Struct for network interface values (transmision, reception, etc..) */
typedef struct iface_info
{
	/* interface name and alias (=display name) */
	char *name;
	char *alias;

	IfaceStatus status;

	time_t last_online;

	/* received bytes */
	double rc_byte;
	double rc_byte_old;

	/* transmited bytes */
	double tr_byte;
	double tr_byte_old;

	/* received packets */
	double rc_pkt;
	double rc_pkt_old;

	/* transmited packets */
	double tr_pkt;
	double tr_pkt_old;
} IfaceInfo;

int machine_init();
int machine_close();

int machine_get_battstat(int *acstat, int *battflag, int *percent);
int machine_get_fs(mounts_type fs[], int *cnt);
int machine_get_load(load_type *cur_load);
int machine_get_loadavg(double *load);
int machine_get_meminfo(meminfo_type *result);
int machine_get_procs(LinkedList *procs);
int machine_get_smpload(load_type *result, int *numcpus);
int machine_get_uptime(double *up, double *idle);
int machine_get_iface_stats (IfaceInfo *interface);


#endif /* _lcdproc_machine_h_ */