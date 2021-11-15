#include "seeker.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define perror_and_exit do { perror(""); exit(EXIT_FAILURE); } while(0)
#define DIR_SEP_SIZE 1

static bool
files_equal(char *f1, char *f2)
{
    int fd1 = open(f1, O_RDONLY);
    int fd2 = open(f2, O_RDONLY);
    if(fd1 == -1 || fd2 == -1)
        perror_and_exit;

    struct stat sb1, sb2;
    if(fstat(fd1, &sb1) == -1 || fstat(fd2, &sb2) == -1)
        perror_and_exit;
    if(sb2.st_size != sb1.st_size)
        return false;

    char *first = mmap(NULL, sb1.st_size, PROT_READ, MAP_PRIVATE, fd1, 0);
    char *second = mmap(NULL, sb2.st_size, PROT_READ, MAP_PRIVATE, fd2, 0);
    if(first == MAP_FAILED || second == MAP_FAILED)
        perror_and_exit;
    for(off_t i = 0; i < sb1.st_size; ++i)
        if(first[i] != second[i])
            return false;

    return true;
}

static void
print_filenames(struct set_node *root, char *suffix)
{
    if(!root)
        return;
    printf("%s%s\n", (char *) root->data, suffix);
    print_filenames(root->left, suffix);
    print_filenames(root->right, suffix);
}

static void
build_name(char *buf, size_t buflen, char *basename, char *entry)
{
    bzero(buf, buflen);
    strcat(buf, basename);
    if(basename[strlen(basename)-1] != '/')
        buf[strlen(basename)] = '/';
    strcat(buf, entry);
}

static char *
build_filename(char *dirbasename, char *dirname, char *entry)
{
    size_t len;
    char *filename;
    if(strlen(dirbasename) == strlen(dirname))
    {
        len = strlen(entry) + 1;
        filename = malloc(len);
        bzero(filename, len);
    }
    else
    {
        char *dir = dirname + strlen(dirbasename);
        if(dirbasename[strlen(dirbasename) - 1] != '/')
            ++dir;
        len = strlen(dir) + DIR_SEP_SIZE + strlen(entry) + 1;
        filename = malloc(len);
        bzero(filename, len);
        strcat(filename, dir);
        filename[strlen(dir)] = '/';
    }
    strcat(filename, entry);

    return filename;
}

void
scan_dir(char *dirname, struct set *files, char *basename)
{
    DIR *dir = opendir(dirname);
    if(!dir)
        perror_and_exit;

    struct dirent *entry;
    errno = 0;
    while((entry = readdir(dir)))
    {
        if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        if(entry->d_type == DT_DIR)
        {
            int namelen = strlen(dirname) + DIR_SEP_SIZE + strlen(entry->d_name) + 1;
            char name[namelen];
            build_name(name, namelen, dirname, entry->d_name);
            scan_dir(name, files, basename);
        }
        else
        {
            char *filename = build_filename(basename, dirname, entry->d_name);
            assert(set_insert(files, filename) != 0);
        }
        errno = 0;
    }
    if(errno)
        perror_and_exit;

    closedir(dir);
}

static bool
is_file_changed(char *filename, char *fdirname, char *sdirname)
{
    int lenf = strlen(filename) + DIR_SEP_SIZE + strlen(fdirname) + 1;
    int lens = strlen(filename) + DIR_SEP_SIZE + strlen(sdirname) + 1;
    char fbuf[lenf];
    char sbuf[lens];
    build_name(fbuf, lenf, fdirname, filename);
    build_name(sbuf, lens, sdirname, filename);

    return !files_equal(fbuf, sbuf);
}

static void
process_node(struct set_node *root, struct set *s, struct metadata *meta)
{
    if(!root)
        return;
    if(set_contains(s, root->data))
    {
        if(is_file_changed(root->data, meta->fdirname, meta->sdirname))
        {
            printf("%s CHANGED\n", (char *) root->data);
            ++meta->stat.num_changed;
        }
        set_remove(s, root->data);
    }
    else
    {
        ++meta->stat.num_removed;
        printf("%s DELETED\n", (char *) root->data);
    }
    process_node(root->right, s, meta);
    process_node(root->left, s, meta);
}

void
print_diffstat(struct diffstat *stat)
{
    printf("Comparison finished in %f seconds. ", stat->time_spent);
    printf("%lu files changed, %lu removed, %lu added\n",
            stat->num_changed, stat->num_removed, stat->num_added);
}

void
seek_diff(struct set *fdir_files, struct set *sdir_files, struct metadata *meta)
{
    printf("Comparing directories \"%s\" and \"%s\"\n", meta->fdirname, meta->sdirname);
    process_node(fdir_files->root, sdir_files, meta);
    meta->stat.num_added += sdir_files->size;
    print_filenames(sdir_files->root, " ADDED");
}