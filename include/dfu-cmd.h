#ifndef __DFU_CMD_H__
#define __DFU_CMD_H__

#include "dfu.h"
#include "dfu-internal.h"

enum dfu_cmd_dir {
	IN,
	OUT,
	OUT_IN,
};

#define START_CHECKSUM		BIT(0)
#define SEND_CHECKSUM		BIT(1)
#define RETRY_ON_ERROR		BIT(2)

struct dfu_cmddescr;

struct dfu_cmdbuf {
	enum dfu_cmd_dir dir;
	int flags;
	struct dfu_cmdbuf_buf {
		void *in;
		const void *out;
	} buf;
	unsigned int len;
	/* Timeout in millisecs */
	unsigned int timeout;
	/*
	 * Optional callback to be invoked on buf completion
	 * Must return 0 if cmd must go on to next buffer, < 0 in case of
	 * errors.
	 */
	int (*completed)(const struct dfu_cmddescr *,
			 const struct dfu_cmdbuf *);
	/* Index of buffer to jump to in case of retry */
	int next_on_retry;
};

enum dfu_cmd_status {
	DFU_CMD_STATUS_OK = 0,
	DFU_CMD_STATUS_INITIALIZED = 1,
	DFU_CMD_STATUS_INTERFACE_READY = 2,
	DFU_CMD_STATUS_WAITING = 3,
	DFU_CMD_STATUS_RETRYING = 4,
	DFU_CMD_STATUS_ERROR = -1,
	DFU_CMD_STATUS_TIMEOUT = -2,
};

struct dfu_cmdstate {
	enum dfu_cmd_status status;
	int cmdbuf_index;
	/* Number of bytes received up to now */
	int received;
	struct dfu_timeout timeout;
};

struct dfu_cmddescr {
	const struct dfu_cmdbuf *cmdbufs;
	int ncmdbufs;
	void *checksum_ptr;
	int checksum_size;
	struct dfu_cmdstate *state;
	/*
	 * Must point to an uninitialized struct dfu_timeout structure
	 * dfu-cmd will fill it in and use it
	 */
	struct dfu_timeout *timeout;
	void (*checksum_reset)(const struct dfu_cmddescr *);
	void (*checksum_update)(const struct dfu_cmddescr *, const void *,
				unsigned int);
	void (*completed)(struct dfu_target *, const struct dfu_cmddescr *);
	void *priv;
};


extern int dfu_cmd_start(struct dfu_target *, const struct dfu_cmddescr *descr);
extern int dfu_cmd_on_interface_event(struct dfu_target *target,
				      const struct dfu_cmddescr *descr);
extern int dfu_cmd_on_idle(struct dfu_target *target,
			   const struct dfu_cmddescr *descr);
/* Synchronously complete command */
extern int dfu_cmd_do_sync(struct dfu_target *target,
			   const struct dfu_cmddescr *descr);

#endif /* __DFU_CMD_H__ */
