/* newsfv.c - creates a new checksum listing
   
   Copyright (C) 2000 Bryan Call <bc@fodder.org>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <libgen.h>
#include <dirent.h>
#include <sys/types.h>

#include "cksfv.h"
#include "config.h"


struct file_entry;

struct file_entry {
    char *fn;
    struct file_entry *next;  /* NULL if no successor */
    struct file_entry *tail;
};

static int processfile(int fd, const char *fn)
{
    uint32_t val;
    char *tmpname;

    if (crc32(fd, &val)) {
	if (!TOTALLY_QUIET)
	    fprintf(stderr, "cksfv: %s: %s\n", fn, strerror(errno));
	return 1;
    } else {
	if (use_basename) {
	    tmpname = strdup(fn);
	    if (tmpname == NULL) {
		if (!TOTALLY_QUIET)
		    fprintf(stderr, "out of memory\n");
		exit(1);
	    }
	    pcrc(basename(tmpname), val);
	    free(tmpname);
	} else {
	    pcrc(fn, val);
	}
    }
    return 0;
}

/*
   Returns 1 when there is a new directory entry.
   Returns 0 when there no more directory entries.
   Returns -1 when the entry should be skipped.
   Returns -2 when there is an error that affects exit code of the process.
*/
static int read_dir_entry(char *fn, const ssize_t maxsize,
			  DIR *dir, const char *directory_to_scan)
{
    struct dirent *dirent;
    dirent = readdir(dir);
    if (dirent == NULL)
	return 0;

    if (strcmp(dirent->d_name, ".") == 0 || strcmp(dirent->d_name, "..") == 0)
	return -1;

    if (snprintf(fn, maxsize, "%s/%s",
		 directory_to_scan, dirent->d_name) >= maxsize) {
	/* A serious error, return -2 */
	if (!TOTALLY_QUIET) {
	    fprintf(stderr, "cksfv: too long a name: %s/%s\n",
		    directory_to_scan, dirent->d_name);
	}
	return -2;
    }

    return 1;
}

static int append_entry(struct file_entry **file_entries, const char *fn)
{
    struct file_entry *e = calloc(1, sizeof(*e));
    if (e == NULL)
	return 1;

    e->fn = strdup(fn);
    if (e->fn == NULL) {
	free(e);
	return 1;
    }

    if (*file_entries == NULL) {
	*file_entries = e;
    } else {
	(*file_entries)->tail->next = e;
    }
    (*file_entries)->tail = e;
    return 0;
}


static int scan_entry(struct file_entry **file_entries, const char *fn,
		      const int recursive, const int follow)
{
    int rval = 0;
    char *tmp = NULL;
    const ssize_t maxsize = PATH_MAX;
    struct stat st;
    DIR *dir = NULL;

    if (stat(fn, &st)) {
	if (!TOTALLY_QUIET) {
	    fprintf(stderr, "cksfv: can not fstat %s: %s\n", fn,
		    strerror(errno));
	}
	goto error;
    }

    if (!S_ISDIR(st.st_mode)) {
	if (append_entry(file_entries, fn))
	    goto error;
	goto end;
    }

    /* fn is a directory */
    if (!recursive) {
	if (!TOTALLY_QUIET)
	    fprintf(stderr, "cksfv: %s: Is a directory\n", fn);
	goto error;
    }

    /* Do not follow symlinks, unless follow != 0 */
    if (lstat(fn, &st)) {
	if (!TOTALLY_QUIET) {
	    fprintf(stderr, "cksfv: can not lstat %s: %s\n",
		    fn, strerror(errno));
	}
	goto error;
    }
    if (!follow && S_ISLNK(st.st_mode))
	goto end;

    dir = opendir(fn);
    if (dir == NULL) {
	if (!TOTALLY_QUIET)
	    fprintf(stderr, "cksfv: Can not read directory %s: %s\n",
		    fn, strerror(errno));
	goto error;
    }

    tmp = malloc(maxsize);
    if (tmp == NULL)
	goto error;

    while (1) {
	int ret = read_dir_entry(tmp, maxsize, dir, fn);
	if (ret == 0) {
	    break;
	} else if (ret < 0) {
	    if (ret == -2)
		rval = 1;
	    continue;
	}

	if (stat(tmp, &st)) {
	    if (!TOTALLY_QUIET) {
		fprintf(stderr, "cksfv: can not fstat %s: %s\n",
			tmp, strerror(errno));
	    }
	    rval = 1;
	    continue;
	}

	/* tmp now contains a directory prefixed name */
	if (S_ISDIR(st.st_mode)) {
	    if (scan_entry(file_entries, tmp, recursive, follow))
		rval = 1;
	} else {
	    if (append_entry(file_entries, tmp))
		rval = 1;
	}
    }

    goto end;

  error:
    rval = 1;

  end:
    if (dir != NULL)
	closedir(dir);
    free(tmp);

    return rval;
}


int newsfv(char **argv, const int recursive, const int follow)
{
    int fd = -1;
    int rval = 0;
    struct file_entry *file_entries = NULL;
    struct file_entry *fe;
    size_t read_index;

    /* First call to newsfv(): print headers */
    pnsfv_head();
    printf(";\n");

    for (read_index = 0; argv[read_index] != NULL; read_index++) {
	if (scan_entry(&file_entries, argv[read_index], recursive, follow))
	    rval = 1;
    }

    /* Print file list into comment section */
    for (fe = file_entries; fe != NULL; fe = fe->next)
	pfileinfo(fe->fn);

    /* Compute checksums */
    for (fe = file_entries; fe != NULL; fe = fe->next) {
	fd = open(fe->fn, O_RDONLY | O_LARGEFILE | O_BINARY, 0);
	if (fd < 0) {
	    if (!TOTALLY_QUIET)
		fprintf(stderr, "cksfv: %s: %s\n", fe->fn, strerror(errno));
	    rval = 1;
	    goto next;
	}

	if (processfile(fd, fe->fn))
	    rval = 1;

      next:
	if (fd >= 0) {
	    close(fd);
	    fd = -1;
	}
    }

    for (fe = file_entries; fe != NULL; ) {
	struct file_entry *next = fe->next;
	free(fe->fn);
	memset(fe, 0, sizeof(*fe));
	free(fe);
	fe = next;
    }

    return rval;
}
