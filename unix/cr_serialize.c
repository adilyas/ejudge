/* -*- mode: c; coding: koi8-r -*- */
/* $Id$ */

/* Copyright (C) 2002 Alexander Chernov <cher@ispras.ru> */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "cr_serialize.h"
#include "prepare.h"
#include "pathutl.h"

#include <reuse/osdeps.h>

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <errno.h>

static int semid = -1;
int
cr_serialize_init(void)
{
  if (!global->cr_serialization_key) return 0;
  if (semid >= 0) return 0;

  semid = semget(global->cr_serialization_key, 1, IPC_CREAT | IPC_EXCL | 0666);
  if (semid < 0) {
    if (errno != EEXIST) {
      err("cr_serialize_init: semget() failed: %s", os_ErrorMsg());
      return -1;
    }
    semid = semget(global->cr_serialization_key, 1, 0);
    if (semid < 0) {
      err("cr_serialize_init: semget() failed: %s", os_ErrorMsg());
      return -1;
    }
    return 0;
  }

  if (semctl(semid, 0, SETVAL, 1) < 0) {
    err("cr_serialize_init: semctl() failed: %s", os_ErrorMsg());
    return -1;
  }
  return 0;
}

int
cr_serialize_lock(void)
{
  struct sembuf ops[1] = {{ 0, -1, SEM_UNDO }};

  if (!global->cr_serialization_key) return 0;
  //info("in cr_serialize_lock %d", semid);
  while (1) {
    if (semop(semid, ops, 1) >= 0) break;
    if (errno == EINTR) {
      info("cr_serialize_lock: interrupted");
      continue;
    }
    err("cr_serialize_lock: semop failed: %s", os_ErrorMsg());
    return -1;
  }
  //info("cr_serialize_lock ok");
  return 0;
}

int
cr_serialize_unlock(void)
{
  struct sembuf ops[1] = {{ 0, 1, SEM_UNDO }};

  if (!global->cr_serialization_key) return 0;
  //info("in cr_serialize_unlock");
  if (semop(semid, ops, 1) < 0) {
    err("cr_serialize_unlock: semop failed: %s", os_ErrorMsg());
    return -1;
  }
  //info("cr_serialize_unlock ok");
  return 0;
}

/**
 * Local variables:
 *  compile-command: "make"
 *  c-font-lock-extra-types: ("\\sw+_t" "FILE")
 *  eval: (set-language-environment "Cyrillic-KOI8")
 * End:
 */
