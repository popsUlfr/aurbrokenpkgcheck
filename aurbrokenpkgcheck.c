/* 
 * MIT License
 *
 * Copyright (c) 2017 Philipp Richter
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <alpm.h>

#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <elf.h>

/* MACROS */
#define LIB_DIR "/lib"
#define LD_PREFIX "ld-linux"
#define LD_PREFIX_LENGTH 8
#define PACMAN_ROOT_PATH_KEY "Root"
#define PACMAN_DB_PATH_KEY "DB Path"
#define BUFFER_SIZE 256
/* MACROS */

/* STRUCTURES */

/* Stores intermediate states between parses */
struct stream_t {
	/* Will be set by stream_parser() if NULL
	 * DO NOT CHANGE */
	char *buffer;
	/* Should reflect the maximum size of the buffer
	 * overwriten if buffer is NULL
	 * DO NOT CHANGE */
	size_t maxsize;
	/* Handled by stream_parser() should not be manually set
	 * DO NO CHANGE */
	ssize_t length;
	/* If left NULL will be set to "\r\n" by stream_parser()
	 * CAN BE CHANGED IN THE CALLBACK */
	const char *delims;
	/* represents the current token position
	 * DO NOT CHANGE */
	int pos;
	/* true if string is the beginning of a new token
	 * DO NOT CHANGE */
	int beg;
	/* true if string is the end of the current token
	 * DO NOT CHANGE */
	int end;
	/* the current '\0' terminated token
	 * DO NOT CHANGE */
	char *string;
	/* the length of the current token
	 * DO NOT CHANGE */
	size_t string_length;
	/* the delimiter that was replaced by '\0' for the current token
	 * DO NOT CHANGE */
	char string_delim;
	/* function to call once more data is available
	 * CAN BE CHANGED IN THE CALLBACK */
	void (*callback)(struct stream_t *);
	/* optional data to pass between calls
	 * CAN BE CHANGED IN THE CALLBACK */
	void *data;
};

/* data for pacman_config_paths parses */
struct stream_pacman_config_paths_t {
	/* will store the root path */
	char *root_path;
	/* maximum allowed size for the root path */
	size_t root_path_maxsize;
	/* real length of the root path */
	size_t root_path_length;
	/* will store the db path */
	char *db_path;
	/* maxium allowed path for the db path */
	size_t db_path_maxsize;
	/* real length of the db path */
	size_t db_path_length;
	/* pointer to the current path */
	char *curr_path;
	/* pointer to the current path maxsize */
	size_t *curr_maxsize;
	/* pointer to the current real length of path */
	size_t *curr_length;
	/* skip flag, 1 = will skip until next line */
	int skip;
	/* root path key */
	const char* root_path_key;
	/* db path key */
	const char* db_path_key;
	/* pointer to the current path key */
	const char* curr_path_key;
};

/* data for foreign_pkgs parses */ 
struct stream_foreign_pkgs_t {
	/* stores the pkgname */
	char pkg[NAME_MAX];
	/* real length of pkg */
	size_t length;
	/* the linked list containing the final pkgnames */
	alpm_list_t *list;
};

/* data for pacman_config_paths stream handler */
struct pacman_config_paths_t {
	/* the root path buffer */
	char *root_path;
	/* the root path maxsize and real length after the call */
	size_t *root_path_length;
	/* the db path buffer */
	char *db_path;
	/* the db path maxsize and real length after the call */
	size_t *db_path_length;
};

/* data for check_package stream handler */
struct check_package_t {
	/* package name */
	const char* pkgname;
	/* filename currently getting checked */
	const char* filename;
	/* the line we are at in the error output */
	int line;
	/* the token position we are at on the line */
	int pos;
	/* is set when a package is determined to be broken */
	int broken;
	/* flag set if the filename has already been printed */
	int filename_printed;
	/* flag sets color output */
	int colors;
};

/* STRUCTURES */

/*
 * error handler
 */
static inline int error_handler(const char* function_name) {
	perror(function_name);
	return errno;
}

/*
 * Init the struct stream_t
 */
static inline void stream_parser_init(struct stream_t *st) {
	memset(st, 0, sizeof(struct stream_t));
}

/*
 * The stream parser reads by blocks instead of line by line
 * The initial and intermediate states are stored in struct stream_t
 * buffer and delims may be empty inside the struct, internal values will be used
 */
static void stream_parser(int fd, struct stream_t *st) {
	char buffer[BUFFER_SIZE];
	if (!st->buffer) {
		st->buffer = buffer;
		st->maxsize = BUFFER_SIZE;
		st->length = 0;
	}
	if (!st->delims) st->delims = "\r\n";
	st->beg = 1;
	st->end = 0;
	/* Subtract one to the size for there to be always the space for '\0' */
	while((st->length = read(fd, st->buffer, (st->maxsize - 1))) > 0) {
		st->buffer[st->length] = 0;
		for (st->string = st->buffer;
			;
			st->string += st->string_length + 1) {
			st->string_length = strcspn(st->string, st->delims);
			if (!st->string_length && !st->string[st->string_length]) break;
			st->string_delim = st->string[st->string_length];
			st->string[st->string_length] = '\0';
			if (st->end) {
				++st->pos;
				st->beg = 1;
				st->end = 0;
			}
			if (st->string_delim) st->end = 1;
			st->callback(st);
			st->beg = 0;
			if (!st->string_delim) break;
		}
	}
}

/*
 * Consumes the whole stream
 */
static inline void noop_stream_handler(int fd, void* data) {
	static char buffer[BUFFER_SIZE];
	(void)data;
	for(;read(fd,buffer, BUFFER_SIZE) > 0;) ;
}

/*
 * Execs a command delegating stdout and stderr output to stream handlers
 * noop_stream_handler for the handlers and NULL for the data may be used
 * Will return the program exit code or -1 if an internal error occurred
 */
static int stream_exec(char *const* argv, 
	void (*stdout_stream_handler)(int,void*),
	void * stdout_data,
	void (*stderr_stream_handler)(int,void*),
	void * stderr_data) {
	int stderr_pipefd[2],stdout_pipefd[2];
	pid_t pid;
	if (pipe(stdout_pipefd) < 0 || pipe(stderr_pipefd) < 0) {
		error_handler("pipe()");
		return -1;
	}
	if ((pid = fork()) == 0) { // child
		close(STDIN_FILENO);
		close(stderr_pipefd[0]);
		close(stdout_pipefd[0]);
		dup2(stderr_pipefd[1], STDERR_FILENO);
		close(stderr_pipefd[1]);
		dup2(stdout_pipefd[1], STDOUT_FILENO);
		close(stdout_pipefd[1]);
		if (execvp(argv[0], argv) < 0) {
			error_handler(argv[0]);
			exit(-1);
		}
	}
	else if (pid < 0) { //parent
		close(stderr_pipefd[0]); close(stderr_pipefd[1]);
		close(stdout_pipefd[0]); close(stdout_pipefd[1]);
		error_handler("fork()");
		return -1;
	}
	else { //parent
		int wstatus;
		close(stderr_pipefd[1]);
		close(stdout_pipefd[1]);
		stderr_stream_handler(stderr_pipefd[0], stderr_data);
		close(stderr_pipefd[0]);
		stdout_stream_handler(stdout_pipefd[0], stdout_data);
		close(stdout_pipefd[0]);
		if (waitpid(pid, &wstatus, 0) < 0) {
			error_handler("waitpid()");
			return -1;
		}
		if (WIFEXITED(wstatus)) return WEXITSTATUS(wstatus);
	}
	return -1;
}

/*
 * Init struct stream_pacman_config_paths_t
 * ATTENTION : root_path, db_path, root_path_maxsize, db_path_maxsize
 * must be set before calling this function
 */
static void stream_parser_pacman_config_paths_init(
	struct stream_pacman_config_paths_t *pst) {
	pst->root_path_length = 0;
	pst->db_path_length = 0;
	pst->curr_path = pst->root_path;
	pst->curr_maxsize = &(pst->root_path_maxsize);
	pst->curr_length = &(pst->root_path_length);
	pst->skip = 0;
	pst->root_path_key = PACMAN_ROOT_PATH_KEY;
	pst->db_path_key = PACMAN_DB_PATH_KEY;
	pst->curr_path_key = pst->root_path_key;
}

/*
 * Stream parser callback for pacman_config_paths
 * st->data is struct stream_pacman_config_paths_t
 * It looks for Root and DB Path
 */
static void stream_parser_pacman_config_paths_callback(struct stream_t *st) {
	struct stream_pacman_config_paths_t * pst;
	size_t len;
	char * trimstart;
	pst = (struct stream_pacman_config_paths_t *)(st->data);
	/* Just return as fast as possible if all the info we needed has been collected */
	if (!pst->curr_path) return;
	if (pst->skip) {
		/* The first token is the key while the second is the path
		 * skip is removed since we arrived at the end of the line
		 */
		if (st->end && st->pos % 2 == 1) pst->skip = 0;
		return;
	}
	trimstart = st->string;
	if (st->beg) {
		*pst->curr_length = 0;
		/* Trims the leading whitespace if any */
		for (; *trimstart == ' ' ; ++trimstart) ;
		len = st->string_length - (size_t)(trimstart - st->string);
	}
	else len = st->string_length;
	/* Here's a check to prevent from writing out of bounds */
	if (*pst->curr_length + len >= *(pst->curr_maxsize)) {
		if (*pst->curr_length < *(pst->curr_maxsize) - 1)
			len = *pst->curr_maxsize - *pst->curr_length - 1;
		else
			len = 0;
	}
	strncpy(pst->curr_path + (*pst->curr_length), trimstart, len);
	(*pst->curr_length) += len;
	if (st->end) {
		char *trimend;
		/* Trim the trailing whitespace */
		for (trimend = pst->curr_path + *pst->curr_length; 
			trimend > pst->curr_path && *(trimend - 1) == ' '; --trimend) ;
		if (trimend == pst->curr_path) {
			/* This means our current token is completely empty, so skip this garbage */
			pst->skip = 1;
			return;
		}
		*pst->curr_length = (size_t)(trimend - pst->curr_path);
		if (st->pos % 2 == 0) {
			/* This is the key token, since we're at the end we can test if it's
			 * the one we want
			 */
			if (*pst->curr_length != strlen(pst->curr_path_key) 
				|| strncmp(pst->curr_path_key, pst->curr_path, *pst->curr_length)) {
				/* It wasn't the one so skip the whole line */
				pst->skip = 1;
				return;
			}
		}
		else {
			/* This is the end of the line, our token is inside our buffer
			 * just set the terminating '\0'
			 */
			pst->curr_path[*pst->curr_length] = 0;
			if (pst->curr_path == pst->root_path) {
				/* For all this to work, it expects the Root key to happend before
				 * the DB Path key.
				 * Once the Root key is done, switch the links to the DB Path
				 */
				pst->curr_path = pst->db_path;
				pst->curr_length = &(pst->db_path_length);
				pst->curr_maxsize = &(pst->db_path_maxsize);
				pst->curr_path_key = pst->db_path_key;
			}
			else {
				/* We got all we need */
				pst->curr_path = NULL;
				pst->curr_length = NULL;
				pst->curr_maxsize = NULL;
				pst->curr_path_key = NULL;
			}
		}
	}
}

/*
 * Stream handler for pacman_config_paths
 * fd is stdout
 * data is struct pacman_config_paths_t
 */
static void pacman_config_paths_stream_handler(int fd, void * data) {
	struct stream_pacman_config_paths_t pst;
	struct stream_t st;
	struct pacman_config_paths_t *pcp = (struct pacman_config_paths_t*)data;
	pst.root_path = pcp->root_path;
	pst.root_path_maxsize = *(pcp->root_path_length);
	pst.db_path = pcp->db_path;
	pst.db_path_maxsize = *(pcp->db_path_length);
	stream_parser_pacman_config_paths_init(&pst);
	stream_parser_init(&st);
	st.callback = stream_parser_pacman_config_paths_callback;
	st.data = &pst;
	st.delims = ":\r\n";
	stream_parser(fd, &st);
	*pcp->root_path_length = pst.root_path_length;
	*pcp->db_path_length = pst.db_path_length;
}

/*
 * Runs pacman --verbose in order to get default root and dbpath
 * root_path_length and db_path_length are pointers to the maximum allowed size
 * of the respective buffers. After the call they will contain the real length
 * of the contents
 */
static int pacman_config_paths(
	char *root_path,
	size_t *root_path_length,
	char *db_path,
	size_t *db_path_length
) {
	struct pacman_config_paths_t pcp;
	char *const cmd[] = { "pacman", "--verbose", NULL };
	pcp.root_path = root_path;
	pcp.root_path_length = root_path_length;
	pcp.db_path = db_path;
	pcp.db_path_length = db_path_length;
	return stream_exec(cmd, 
		pacman_config_paths_stream_handler, &pcp, noop_stream_handler, NULL);
}

/* Just a filter checking for the allowed ld prefix for scandir() */
static inline int ld_filter(const struct dirent* entry) {
	return !strncmp(LD_PREFIX, entry->d_name, LD_PREFIX_LENGTH);
}

/*
 * Finds the best ld for bin
 * The ld path is stored inside ld_bin_path
 * Anything other than 0 returned is an error
 */
static int ld_bin_finder(char * ld_bin_path, size_t ld_bin_path_length, char* bin) {
	int nbentries, i, last_error;
	struct dirent **list;
	char *const cmd[] = {ld_bin_path, "--verify", bin, NULL};
	last_error = 0;
	if ((nbentries = scandir(LIB_DIR, &list, ld_filter, alphasort)) < 0)
		return error_handler("scandir()");
	if (nbentries <= 0) return 1;
	for (i = 0; i < nbentries; ++i) {
		struct stat statbuf;
		int ret;
		last_error = 0;
		snprintf(ld_bin_path, ld_bin_path_length, "%s/%s", LIB_DIR, list[i]->d_name);
		free(list[i]);
		
		if (stat(ld_bin_path, &statbuf) < 0) {
			last_error = error_handler(ld_bin_path);
			continue;
		}
		
		/* Check if it's user executable */
		if (!S_ISREG(statbuf.st_mode) || !(statbuf.st_mode & S_IXUSR)) {
			last_error = 1;
			continue;
		}
		
		/* The ld --verify command may return 0 or 2 in order to catch the compatible ld */
		if ((ret = stream_exec(
			cmd,
			noop_stream_handler,
			NULL,
			noop_stream_handler,
			NULL)) == 0 || ret == 2) break;
	}
	for (; ++i < nbentries;) free(list[i]);
	free(list);
	return last_error;
}

/*
 * Init struct stream_foreign_pkgs_t
 */
static inline void stream_parser_foreign_pkgs_init(struct stream_foreign_pkgs_t *sfp) {
	sfp->length = 0;
	sfp->list = NULL;
}

/*
 * Stream parser callback for foreign_packages
 * st->data is struct stream_foreign_pkgs_t
 */
static void stream_parser_foreign_pkgs_callback(struct stream_t *st) {
	struct stream_foreign_pkgs_t *sfp = (struct stream_foreign_pkgs_t*)(st->data);
	if (st->beg) sfp->length = 0;
	strncpy(sfp->pkg + sfp->length, st->string, st->string_length);
	sfp->length += st->string_length;
	if (st->end) {
		sfp->pkg[sfp->length] = 0;
		sfp->list = alpm_list_add(sfp->list, strdup(sfp->pkg));
	}
}

/*
 * Stream handler for foreign_packages
 * fd is stdout
 * data is alpm_list_t**
 */
static void foreign_packages_stream_handler(int fd, void * data) {
	struct stream_t st;
	struct stream_foreign_pkgs_t sfp;
	alpm_list_t **list = (alpm_list_t **)data;
	stream_parser_foreign_pkgs_init(&sfp);
	sfp.list = *list;
	stream_parser_init(&st);
	st.delims = "\r\n";
	st.callback = stream_parser_foreign_pkgs_callback;
	st.data = &sfp;
	stream_parser(fd, &st);
	*list = sfp.list;
}

/*
 * Stores the foreign packages in list
 * Anything other than 0 returned is an error
 * However even if list is set to NULL, it needs to be freed afterwards
 */
static inline int foreign_packages(
	alpm_list_t **list,
	char* root_path,
	char* db_path) {
	char *const cmd[] = { 
		"pacman",
		"--root", root_path,
		"--dbpath", db_path,
		"--query", 
		"--foreign", 
		"--quiet", 
		NULL };
	return stream_exec(
		cmd, 
		foreign_packages_stream_handler, 
		list, 
		noop_stream_handler, 
		NULL);
}

/*
 * Simple check for the ELF magic bytes on the file
 * Anything other than 0 returned is not an ELF or an error
 */
static int check_for_elf_header(const char* filename) {
	int fd;
	char elfbuffer[4];
	ssize_t len;
	if ((fd = open(filename, O_RDONLY)) < 0)
		return error_handler(filename);
	if ((len = read(fd, elfbuffer, 4)) < 0) {
		int ret = error_handler(filename);
		close(fd);
		return ret;
	}
	close(fd);
	return (len != 4
		|| elfbuffer[0] != ELFMAG0 
		|| elfbuffer[1] != ELFMAG1
		|| elfbuffer[2] != ELFMAG2
		|| elfbuffer[3] != ELFMAG3);
}

/*
 * The stream parser callback for check_package
 * st->data carries a struct check_package_t
 */
static void stream_parser_check_package_callback(struct stream_t *st) {
	struct check_package_t* cpt = (struct check_package_t*)(st->data);
	if (!cpt->broken) {
		/* Print the stdout package name only once */
		if (cpt->colors) fprintf(stdout, "\033[0;34m%s\033[0m\n", cpt->pkgname);
		else fprintf(stdout, "%s\n", cpt->pkgname);
		cpt->broken = 1;
	}
	if (!cpt->filename_printed) {
		/* This is the filename line */
		fprintf(stderr, "    └── %s\n", cpt->filename);
		cpt->filename_printed = 1;
	}
	if (st->beg && cpt->pos == 2)  {
		/* We are positioned directly after the second colon, start printing */
		if (cpt->colors) fprintf(stderr, "        └──\033[0;31m");
		else fprintf(stderr, "        └──");
	}
	if (cpt->pos > 1) 
		fprintf(stderr, "%.*s", (int)(st->string_length), st->string);
	if (st->end) {
		if (st->string_delim == '\r' || st->string_delim == '\n') {
			/* We hit the end of a line here */
			if (cpt->colors) fprintf(stderr, "\033[0m\n");
			else fprintf(stderr, "\n");
			++cpt->line;
			cpt->pos = 0;
		}
		else {
			/* The stream parser overwrote the delimiter, so we print it ourselves */
			if (cpt->pos > 1) fprintf(stderr, "%c", st->string_delim);
			++cpt->pos;
		}
	}
}

/*
 * Stream handler callback for check_package
 * fd is the error output
 * data is a struct check_package_t
 */
static void check_package_stream_handler(int fd, void * data) {
	struct stream_t st;
	stream_parser_init(&st);
	/* The goal is to start printing the ld error output after the second colon
	 * Anything before that is redundant information
	 */
	st.delims = ":\r\n";
	st.callback = stream_parser_check_package_callback;
	st.data = data;
	stream_parser(fd, &st);
}

/*
 * Checks a package for broken dependencies
 * colors enables/disables colored output
 * 
 * Anything other than 0 returned is a fatal error
 */
static int check_package(
	alpm_handle_t *handle,
	alpm_db_t *db_local,
	const char* pkgname,
	const char* root_path,
	int colors) {
	alpm_pkg_t *pkg;
	alpm_filelist_t *filelist;
	size_t i;
	char filename[PATH_MAX];
	char ld_bin_path[PATH_MAX];
	char * slash;
	int has_ending_slash;
	struct check_package_t cpt;
	/* Calling ld with --list will produce error output in case of broken lib */
	char *const cmd[] = {ld_bin_path, "--list", filename, NULL};
	
	if (!(pkg = alpm_db_get_pkg(db_local, pkgname))) {
		fprintf(stderr, "%s\n", alpm_strerror(alpm_errno(handle)));
		return 1;
	}
	if (!(filelist = alpm_pkg_get_files(pkg))) {
		alpm_pkg_free(pkg);
		fprintf(stderr, "%s\n", alpm_strerror(alpm_errno(handle)));
		return 1;
	}
	has_ending_slash = ((slash = strrchr(root_path, '/')) && slash[1] == 0);
	cpt.pkgname = pkgname;
	cpt.filename = filename;
	cpt.broken = 0;
	cpt.colors = colors;
	for (i = 0; i < filelist->count; ++i) {
		struct stat statbuf;
		/* If the name ends with a '/' then it's a directory */
		if ((slash = strrchr(filelist->files[i].name, '/')) && slash[1] == 0)
			continue;
		/* Filenames do not have a leading '/' */
		snprintf(filename, PATH_MAX, "%s%s%s", 
			root_path, (has_ending_slash)?"":"/", filelist->files[i].name);
		if (stat(filename, &statbuf) < 0) {
			/* Not caring about handling stat errors */
			continue;
		}
		/* Check if the file is user executable */
		if (!S_ISREG(statbuf.st_mode) || !(statbuf.st_mode & S_IXUSR))
			continue;
		/* We are only interested in ELF files, so quickly check the header */
		if (check_for_elf_header(filename)) continue;
		/* Find the correct ld binary which can do something useful with the file */
		if (ld_bin_finder(ld_bin_path, PATH_MAX, filename))
			continue;
		cpt.line = 0;
		cpt.pos = 0;
		cpt.filename_printed = 0;
		/* Exec ld on our file, only stderr output is interesting */
		stream_exec(
			cmd,
			noop_stream_handler,
			NULL,
			check_package_stream_handler,
			&cpt);
	}
	alpm_pkg_free(pkg);
	return 0;
}

static void usage(const char* arg0) {
	fprintf(stdout, "Usage: %s [-h|--help] [-b|--dbpath DBPATH] [-r|--root ROOT] [--colors] [--no-colors]\n", arg0);
	fprintf(stdout, "Options:\n");
	fprintf(stdout, "\t -h,--help          : This help\n");
	fprintf(stdout, "\t -b,--dbpath DBPATH : The database location to use (see man 8 pacman)\n");
	fprintf(stdout, "\t -r,--root ROOT     : The installation root to use (see man 8 pacman)\n");
	fprintf(stdout, "\t --colors           : Enable colored output (default)\n");
	fprintf(stdout, "\t --no-colors        : Disable colored output\n");
}

int main(int argc, const char* argv[]) {
	alpm_list_t *list,*i;
	alpm_db_t *db_local;
	alpm_errno_t err;
	alpm_handle_t *handle;
	const char** arg;
	char root_path[PATH_MAX],db_path[PATH_MAX];
	size_t root_path_length,db_path_length;
	int colors;
	(void)argc;
	root_path_length = PATH_MAX;
	db_path_length = PATH_MAX;
	colors = 1;
	/* The default pacman paths are taken from its verbose output */
	if (pacman_config_paths(
		root_path, &root_path_length,
		db_path, &db_path_length) < 0 || !root_path_length || !db_path_length) {
		return EXIT_FAILURE;
	}
	for (arg = argv + 1; *arg ; ++arg) {
		if (!strcmp(*arg, "-b") || !strcmp(*arg, "--dbpath")) {
			if (!*(++arg)) {
				fprintf(stderr, "Missing argument for '%s'\n", *(arg - 1));
				usage(*argv);
				return EXIT_FAILURE;
			}
			strncpy(db_path, *arg, PATH_MAX);
		}
		else if (!strcmp(*arg, "-r") || !strcmp(*arg, "--root")) {
			if (!*(++arg)) {
				fprintf(stderr, "Missing argument for '%s'\n", *(arg - 1));
				usage(*argv);
				return EXIT_FAILURE;
			}
			strncpy(root_path, *arg, PATH_MAX);
		}
		else if (!strcmp(*arg, "-h") || !strcmp(*arg, "--help")) {
			usage(*argv);
			return EXIT_SUCCESS;
		}
		else if (!strcmp(*arg, "--colors")) {
			colors = 1;
		}
		else if (!strcmp(*arg, "--no-colors")) {
			colors = 0;
		}
		else {
			fprintf(stderr, "Unknown option '%s'\n", *arg);
			usage(*argv);
			return EXIT_FAILURE;
		}
	}
	/* Print the used paths */
	fprintf(stderr, "%-8s : %s\n", PACMAN_ROOT_PATH_KEY, root_path);
	fprintf(stderr, "%-8s : %s\n", PACMAN_DB_PATH_KEY, db_path);
	list = NULL;
	/* This calls pacman for foreign packages and loads them into our list */
	if (foreign_packages(&list, root_path, db_path)) {
		FREELIST(list);
		return EXIT_FAILURE;
	}
	/* Initialize alpm handle */
	if (!(handle = alpm_initialize(root_path, db_path, &err))) {
		fprintf(stderr, "%s\n", alpm_strerror(err));
		return EXIT_FAILURE;
	}
	if (!(db_local = alpm_get_localdb(handle))) {
		fprintf(stderr, "%s\n", alpm_strerror(alpm_errno(handle)));
		return EXIT_FAILURE;
	}
	/* Check each package for broken libs or binaries */
	for (i = list; i; i = alpm_list_next(i))
		check_package(handle, db_local, (char*)(i->data), root_path, colors);
	FREELIST(list);
	/* Always release the handle */
	if (alpm_release(handle) < 0) {
		fprintf(stderr, "%s\n", alpm_strerror(alpm_errno(handle)));
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
