/*
 * "git add" builtin command
 *
 * Copyright (C) 2006 Linus Torvalds
 */
#include <fnmatch.h>

#include "cache.h"
#include "builtin.h"
#include "dir.h"

static const char builtin_add_usage[] =
"git-add [-n] [-v] <filepattern>...";

static int common_prefix(const char **pathspec)
{
	const char *path, *slash, *next;
	int prefix;

	if (!pathspec)
		return 0;

	path = *pathspec;
	slash = strrchr(path, '/');
	if (!slash)
		return 0;

	prefix = slash - path + 1;
	while ((next = *++pathspec) != NULL) {
		int len = strlen(next);
		if (len >= prefix && !memcmp(path, next, len))
			continue;
		for (;;) {
			if (!len)
				return 0;
			if (next[--len] != '/')
				continue;
			if (memcmp(path, next, len+1))
				continue;
			prefix = len + 1;
			break;
		}
	}
	return prefix;
}

static int match(const char **pathspec, const char *name, int namelen, int prefix)
{
	const char *match;

	name += prefix;
	namelen -= prefix;

	while ((match = *pathspec++) != NULL) {
		int matchlen;

		match += prefix;
		matchlen = strlen(match);
		if (!matchlen)
			return 1;
		if (!strncmp(match, name, matchlen)) {
			if (match[matchlen-1] == '/')
				return 1;
			switch (name[matchlen]) {
			case '/': case '\0':
				return 1;
			}
		}
		if (!fnmatch(match, name, 0))
			return 1;
	}
	return 0;
}

static void prune_directory(struct dir_struct *dir, const char **pathspec, int prefix)
{
	int i;
	struct dir_entry **src, **dst;

	src = dst = dir->entries;
	i = dir->nr;
	while (--i >= 0) {
		struct dir_entry *entry = *src++;
		if (!match(pathspec, entry->name, entry->len, prefix)) {
			free(entry);
			continue;
		}
		*dst++ = entry;
	}
	dir->nr = dst - dir->entries;
}

static void fill_directory(struct dir_struct *dir, const char **pathspec)
{
	const char *path, *base;
	int baselen;

	/* Set up the default git porcelain excludes */
	memset(dir, 0, sizeof(*dir));
	dir->exclude_per_dir = ".gitignore";
	path = git_path("info/exclude");
	if (!access(path, R_OK))
		add_excludes_from_file(dir, path);

	/*
	 * Calculate common prefix for the pathspec, and
	 * use that to optimize the directory walk
	 */
	baselen = common_prefix(pathspec);
	path = ".";
	base = "";
	if (baselen) {
		char *common = xmalloc(baselen + 1);
		common = xmalloc(baselen + 1);
		memcpy(common, *pathspec, baselen);
		common[baselen] = 0;
		path = base = common;
	}

	/* Read the directory and prune it */
	read_directory(dir, path, base, baselen);
	if (pathspec)
		prune_directory(dir, pathspec, baselen);
}

static int add_file_to_index(const char *path, int verbose)
{
	int size, namelen;
	struct stat st;
	struct cache_entry *ce;

	if (lstat(path, &st))
		die("%s: unable to stat (%s)", path, strerror(errno));

	if (!S_ISREG(st.st_mode) && !S_ISLNK(st.st_mode))
		die("%s: can only add regular files or symbolic links", path);

	namelen = strlen(path);
	size = cache_entry_size(namelen);
	ce = xcalloc(1, size);
	memcpy(ce->name, path, namelen);
	ce->ce_flags = htons(namelen);
	fill_stat_cache_info(ce, &st);

	ce->ce_mode = create_ce_mode(st.st_mode);
	if (!trust_executable_bit) {
		/* If there is an existing entry, pick the mode bits
		 * from it.
		 */
		int pos = cache_name_pos(path, namelen);
		if (pos >= 0)
			ce->ce_mode = active_cache[pos]->ce_mode;
	}

	if (index_path(ce->sha1, path, &st, 1))
		die("unable to index file %s", path);
	if (add_cache_entry(ce, ADD_CACHE_OK_TO_ADD))
		die("unable to add %s to index",path);
	if (verbose)
		printf("add '%s'\n", path);
	return 0;
}

static struct cache_file cache_file;

int cmd_add(int argc, const char **argv, char **envp)
{
	int i, newfd;
	int verbose = 0, show_only = 0;
	const char *prefix = setup_git_directory();
	const char **pathspec;
	struct dir_struct dir;

	git_config(git_default_config);

	newfd = hold_index_file_for_update(&cache_file, get_index_file());
	if (newfd < 0)
		die("unable to create new cachefile");

	if (read_cache() < 0)
		die("index file corrupt");

	for (i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (arg[0] != '-')
			break;
		if (!strcmp(arg, "--")) {
			i++;
			break;
		}
		if (!strcmp(arg, "-n")) {
			show_only = 1;
			continue;
		}
		if (!strcmp(arg, "-v")) {
			verbose = 1;
			continue;
		}
		die(builtin_add_usage);
	}
	git_config(git_default_config);
	pathspec = get_pathspec(prefix, argv + i);

	fill_directory(&dir, pathspec);

	if (show_only) {
		const char *sep = "", *eof = "";
		for (i = 0; i < dir.nr; i++) {
			printf("%s%s", sep, dir.entries[i]->name);
			sep = " ";
			eof = "\n";
		}
		fputs(eof, stdout);
		return 0;
	}

	for (i = 0; i < dir.nr; i++)
		add_file_to_index(dir.entries[i]->name, verbose);

	if (active_cache_changed) {
		if (write_cache(newfd, active_cache, active_nr) ||
		    commit_index_file(&cache_file))
			die("Unable to write new index file");
	}

	return 0;
}
