#include "param.h"
#include "systm.h"
#include "process.h"
#include "ipc.h"
#include "memory.h"
#include "type.h"
#include "errno.h"

/* for memset */
#include <string.h>

/* Now all we support are ipc routines directly ported from sysv.
 * Someday POSIX ipc will be supported! */

/* Here argument size will be the size of the raw message to be passed. 
 * If size is too big to be packed in one msg unit, split it. */
int msg_send(int msg_id, const void *ptr, size_t size, mode_t flag)
{
	msg_queue_attr mqa;
	msg message, head;
	msg last_message == NULL;
	int i;

	if (msg_id < 0)
		return EINVAL;

	for (i = 0, mqa = mqueue_attr; i < msg_id; i++, mqa = mqa->next);

	if (mqa == NULL)
		return EINVAL;
	if (mqa->max_bytes == 0)
		return EINVAL;
	
	/* Found what we want. */
	for (;;) {
		int need_more_resources = 0;
		
		if (size > mqa->max_bytes)
			return EINVAL;
		if (mqa->msg_ipc.mode & MSG_LOCKED)
			need_more_resources = 1;
		if (size + mqa->total_bytes > mqa->max_bytes)
			need_more_resources = 1;

		if (need_more_resources) {
			int we_own_it;

			if ((flag & IPC_NOWAIT) != 0)
				/* We can't wait for a resource. */
				return EAGAIN;

			/* FIXME: Question: is a lock required? */
			if ((mqa->msg_ipc.mode & MSG_LOCKED) != 0)
				we_own_it = 0;
			else {
				mqa->msg_ipc.mode |= MSG_LOCKED;
				we_own_it = 1;
			}
			/* FIXME: And should we prevent incoming requests? */
#if 0
			errno = msg_sleep(mqa);
#endif
			if (we_own_it)
				mqa->msg_ipc.mode &= ~MSG_LOCKED;
			if (errno != 0) {
				/* Interrupted. */
				errno = EINTR;
				return EINTR;
			}

			if (mqa->max_bytes == 0)
				return EIDRM;
		} else
			break;
	}

	if (mqa->msg_ipc.mode & MSG_LOCKED)
		panic("MSG_LOCKED");
	if (size + mqa->total_bytes > mqa->max_bytes)
		panic("size + total_bytes > max_bytes");

	mqa->msg_ipc.mode |= MSG_LOCKED;

	/* Initialize the message body with content pointed by 'ptr'.
	 * The list will have form msg1->msg2->msg3... */
	while (size > 0) {
		size_t len;

		if ((message = (msg)malloc(sizeof(struct msg))) == NULL)
			return ENOMEM;
		if (size > MAX_MSGS)
			len = MAX_MSGS;
		else
			len = size;
		memcpy(message->text, ptr, len);
		size -= len;
		ptr = (const char *)ptr + len;

		if (last_message == NULL) {
			last_message = message;
			head = message;
		}
		else {
			last_message->next = message;
			last_message = message;
		}
	}

	mqa->msg_ipc.mode &= ~MSG_LOCKED;
	
	/* Makesure the attr still alive. */
	if (mqa->max_bytes == 0) {
		for (message = mqa->first; message->next != NULL;
				message = message->next)
			free(message);
		return EIDRM;
	}

	/* Enqueue the new message(s). */
	if (mqa->first == NULL) {
		mqa->first = head;
		for (message = head, i = 1; message->next != NULL;
				message = message->next, i++);
		mqa->last = message;
	} else {
		mqa->last->next = head;
		for (message = head, i = 1; message->next != NULL;
				message = message->next, i++);
		mqa->last = message;
	}
	mqa->last->next = NULL;
	mqa->total_bytes += size;
	mqa->total_msgs += i;
	mqa->last_spid = curproc->pid;
	mqa->stime = curtime;

	return ENONE;
}

int msg_recv(int msg_id, void *ptr, size_t size, mode_t flag)
{
	msg_queue_attr mqa;
	msg message;
	size_t len = 0;
	int i;

	if (msg_id < 0)
		return EINVAL;

	for (i = 0, mqa = mqueue_attr; i < msg_id; i++, mqa = mqa->next);

	if (mqa == NULL)
		return EINVAL;
	if (mqa->max_bytes == 0)
		return EINVAL;
	/* We get what we want. */

	message = NULL;

	/* Reuse the counter. */
	i = 0;
	while (message == NULL) {
		msg prev;
		msg *prev_ptr;

		prev = NULL;
		prev_ptr = &(mqa->first);
		while ((message = *prev_ptr) != NULL) {
			/* Increment the total extracted length. */
			len += message->size;
			i++;
			prev = message;
			prev_ptr = &(message->next);
		}

		if (message != NULL)
			break;

		/* No message but no wait. */
		if ((flag & IPC_NOWAIT) != 0)
			return ENOMSG;

		/* We have time to wait. */
#if 0
		errno = msg_sleep(mqa);
#endif
		if (errno != 0) {
			errno = EINTR;
			return EINTR;
		}

		if (mqa->max_bytes == 0)
			return EIDRM;
	}

	mqa->total_bytes -= len;
	mqa->total_msgs -= i;
	mqa->last_rpid = curproc->pid;
	mqa->rtime = curtime;

	if (size > len)
		size = len;

	for (len = 0, message = mqa->first; len < size;
			len += MAX_MSGS, message = message->next) {
		size_t tlen;

		if (size - len > MAX_MSGS)
			tlen = MAX_MSGS;
		else
			tlen = size - len;
		memcpy(ptr, message->text, tlen);
	}

	return ENONE;
}

/* Return a msg_id. -1 means error, msg_id start from 0. */
int msg_get(key_t key, mode_t flag)
{
	msg_queue_attr mqa;
	mapent m;
	int nth = 0;

	if (key != IPC_PRIVATE)
#ifdef UXXX_FS
	{
		for (mqa = mqueue_attr; mqa != NULL; mqa = mqa->next) {
			if ((mqa->msg_ipc.key == key) && (mqa->max_bytes > 0))
				break;
			nth++;
		}
		/* We've not found a corresponding entry. */
		if (mqa == NULL) {
			/* We are not out of space, we are out of 'the' resource. */
			errno = ENOSPC;
			goto error;
		}

		if ((flag & IPC_CREAT) && (flag & IPC_EXCL)) {
			errno = EEXIST;
			goto error;
		}

		goto found;
	}
#else
	{
		errno = ENODEV;
		goto error;
	}
#endif
	else {
		if (flag & IPC_CREAT) {
			for (mqa = mqueue_attr; mqa->next != NULL; mqa = mqa->next)
				nth++;
			/* Now we get the last msg_queue_attr. */
			if (memory_alloc(m, sizeof(struct msg_queue_attr))) {
				errno = ENOMEM;
				goto error;
			}

			/* We are successful, create one new msg queue and its attr. */
			mqa->next = (msg_queue_attr)m->addr;
			mqa = mqa->next;
			nth++;
			mqa->msg_ipc.key = key;
			/* TODO: Is this argument superfluous? */
			mqa->msg_ipc.mode = flag & 0777;
			mqa->first = NULL;
			mqa->last = NULL;
			mqa->total_bytes = 0;
			mqa->total_msgs = 0;
			/* The up limit. */
			mqa->max_bytes = MAX_MSGQ;
			mqa->last_spid = 0;
			mqa->last_rpid = 0;
			mqa->stime = 0;
			mqa->rtime = 0;
			mqa->ctime = curtime;
			mqa->next = NULL;
		} else {
			/* We didn't find and we are done. */
			errno = ENOENT;
			goto error;
		}
	}

found:
	return nth;
error:
	return -1;
}

/* FIXME: Since we are using linked list, does it mean we don't need to pass
 * a msg_queue_attr struct pointer? To us there's no kernel structure,
 * and none of them are pre-allocated... */
int msg_ctrl(int msg_id, int cmd, msg_queue_attr attr)
{
	msg_queue_attr mqa, mqa2;
	int i;

	if (msg_id < 0)
		return EINVAL;

	for (i = 0, mqa = mqueue_attr; i < msg_id; i++, mqa = mqa->next);

	/* Not found. */
	if (mqa == NULL)
		return EINVAL;

	/* Then we have the msg_queue_attr we need! */
	switch (cmd) {
	case IPC_STAT:
		/* Copy the 'msg_id'th msg_queue_attr to msg_queue_attr. */
	{
		attr = mqa;
		/* Fall to the normal return. */
		break;
	}
	case IPC_SET:
	{
		if (mqa->max_bytes == 0) {
			errno = EINVAL;
			goto error;
		}
		mqa->msg_ipc.mode = (mqa->msg_ipc.mode & ~0777) |
			(attr->msg_ipc.mode & 0777);
		mqa->max_bytes = attr->max_bytes;
		mqa->ctime = curtime;
		break;
	}
	case IPC_RMID:
	{
		msg msg_header;

		msg_header = mqa->first;
		while (msg_header != NULL) {
			msg temp;
			mqa->total_bytes -= msg_header->size;
			mqa->total_msgs--;
			temp = msg_header;
			msg_header = msg_header->next;
			free(temp);
		}

		/* All free, and make the maximum size 0, then free the entry. */
		if (mqa->total_bytes != 0 || mqa->total_msgs != 0)
			panic("msg_ctrl");
		mqa->max_bytes = 0;

		/* Find the entry before the one we want to remove. */
		for (mqa2 = mqueue_attr; mqa2->next != mqa; mqa2 = mqa2->next);
		mqa2->next = mqa->next;
		free(mqa);
		break;
	}
	default:
		return EINVAL;
	}

	return ENONE;
}

int sem_op(int sem_id, sem_buf buf, size_t nops)
{
}

/* Return a sem_id */
int sem_get(key_t key, int count, mode_t flag)
{
	sem_queue_attr sqa;
	mapent m;
	int nth = 0;

	if (key != IPC_PRIVATE)
#ifdef UXXX_FS
	{
		for (sqa = semqueue_attr; sqa != NULL; sqa = sqa->next) {
			if ((sqa->sem_ipc.key == key) && (sqa->sem_ipc.mode & SEM_ALLOC))
				break;
			nth++;
		}

		if (count <= 0) {
			errno = EINVAL;
			goto error;
		}

		if ((count > 0) && (sqa->total_sems < count)) {
			errno = EINVAL;
			goto error;
		}

		if ((flag & IPC_CREAT) && (flag & IPC_EXCL)) {
			errno = EEXIST;
			goto error;
		}

		goto found;
	}
#else
	{
		errno = ENODEV;
		goto error;
	}
#endif
	else {
		if (flag & IPC_CREAT) {
			if (count <= 0 || (count + sem_ptr) > MAX_SEMS) {
				errno = EINVAL;
				goto error;
			}

			for (sqa = semqueue_attr; sqa->next != NULL; sqa = sqa->next)
				nth++;
			/* Now we are faced with the last sem_queue_attr. */
			if (memory_alloc(m, sizeof(struct sem_queue_attr))) {
				errno = ENOMEM;
				goto error;
			}

			sqa->next = (sem_queue_attr)m->addr;
			sqa = sqa->next;
			sqa->ipc.key = key;
			sqa->ipc.mode = mode;
			sqa->first = &sems[sem_ptr];
			sqa->total_sems = (uint16_t)count;
			sem_ptr += total_sems;
			memset(sqa->first, 0, sizeof(sem) * total_sems);
			sqa->otime = 0;
			sqa->ctime = curtime;
			sqa->next = NULL;
		} else {
			errno = ENOENT;
			goto error;
		}
	}

found:
	return nth;

error:
	return -1;
}

int sem_ctrl(int sem_id, int count, int cmd, union semun args)
{
	switch (cmd) {
	case IPC_STAT:
		break;
	case IPC_SET:
		break;
	case IPC_RMID:
		break;
	case GETVAL:
		break;
	case SETVAL:
		break;
	case GETPID:
		break;
	case GETNCNT:
		break;
	case GETZCNT:
		break;
	case GETALL:
		break;
	case SETALL:
		break;
	default:
		goto done2;
	}

done2:
	return EINVAL;
}

void *shm_at(int shm_id, const void *addr, mode_t flag)
{
}

int shm_dt(const void *addr)
{
}

/* Return a shm_id 
 * FIXME: We don't have a pager,
 * and so we just use a map entry as a shm object. */
static int shm_get_alloc(key_t key, size_t size, mode_t flag)
{
	mapent m;
	shm_queue_attr sqa;
	int nth = 0;

	for (sqa = shmqueue_attr; sqa->next != NULL; sqa = sqa->next)
		nth++;

	/* We get the last shm_queue_attr. */
	if (errno = memory_alloc(m, sizeof(struct shm_queue_attr)))
		goto error;

	sqa->next = (shm_queue_attr)m->addr;
	sqa = sqa->next;
	sqa->shm_ipc.key = key;
	sqa->shm_ipc.mode = mode;
	sqa->start = shm_raw_ptr;
	sqa->size = size;
	shm_raw_ptr = (void *)((char *)shm_raw_ptr + size);
	sqa->lopid = 0;
	/* FIXME: The creator is the running one? */
	sqa->cpid = curproc->pid;
	sqa->attach = 0;
	sqa->atime = 0;
	sqa->dtime = 0;
	sqa->ctime = curtime;
	sqa->next = NULL;

found:
	return nth;
error:
	return -1;
}

int shm_get(key_t key, size_t size, mode_t flag)
{
	shm_queue_attr sqa;
	int nth = 0;

	if (key != IPC_PRIVATE)
#ifdef UXXX_FS
	{
		/* Find a shm with key. */
		for (saq = shmqueue_attr; saq != NULL; saq = saq->next) {
			if (saq->shm_ipc.key == key &&
					saq->shm_ipc.mode & SHMSEG_ALLOCATED)
				goto found;
		}

		/* Not found, then check the flag. */
		if ((flag & (IPC_CREAT | IPC_EXCL)) == (IPC_CREAT | IPC_EXCL)) {
			errno = EEXIST;
			goto error;
		} else if ((flag & IPC_CREAT) == 0) {
			errno = ENOENT;
			goto error;
		} else if ((uintptr_t)((char *)shm_raw_ptr + size) >
				(uintptr_t)((char *)shm_pool->addr + MAX_SHMS)) {
			/* If we've checked this, we know alloc will have a legal extent. */
			errno = EINVAL;
			goto error;
		} else {
			if ((nth = shm_get_alloc(key, size, flag)) == -1)
				goto error;
			goto found;
		}
	}
#else
	{
		errno = ENODEV;
		goto error;
	}
#endif
	else {
		if (flag & IPC_CREAT) {
			if ((uintptr_t)((char *)shm_raw_ptr + size) >
					(uintptr_t)((char *)shm_pool->addr + MAX_SHMS)) {
				errno = EINVAL;
				goto error;
			}
			if ((nth = shm_get_alloc(key, size, mode)) == -1)
				goto error;
			goto found;
		} else {
			errno = ENOENT;
			goto error;
		}
	}

found:
	return nth;

error:
	return -1;
}

int shm_ctrl(int shm_id, int cmd, shm_queue_attr attr)
{
	switch (cmd) {
	case IPC_STAT:
		break;
	case IPC_SET:
		break;
	case IPC_RMID:
		break;
	case SHM_LOCK:
		break;
	case SHM_UNLOCK:
		break;
	default:
		goto done2;
	}

done2:
	return EINVAL;
}

int ipc_init()
{
	mqueue_attr = NULL;
	semqueue_attr = NULL;
	memset(sems, 0, sizeof(struct sem) * MAX_SEMS);
	shmqueue_attr = NULL;
	if (errno = memory_alloc(shm_pool, MAX_SHMS))
		return errno;
	return ENONE;
}

