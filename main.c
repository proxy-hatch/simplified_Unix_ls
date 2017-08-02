//// This project is created to fulfill CMPT300 Assignment 4 requirements
//// It consists of a simplified version of the 'ls' cmd that we often take for granted in UNIX
////
//// The program only supports -iRl flags, with some modifications, see the assignment instruction sheet for details
////
//// Created on: Jul 25, 2017
//// Last Modified: Aug , 2017
//// Author: Yu Xuan (Shawn) Wang
//// Email: yxwang@sfu.ca
//// Student #: 301227972
////
/* We want POSIX.1-2008 + XSI, i.e. SuSv4, features */
#define _XOPEN_SOURCE 700

/* If the C library can support 64-bit file sizes
   and offsets, using the standard names,
   these defines tell the C library to do so. */
#define _LARGEFILE64_SOURCE
#define _FILE_OFFSET_BITS 64

/* "The feature test macro _GNU_SOURCE must be defined in order
 * to obtain the definition of FTW_ACTIONRETVAL from <ftw.h>."
 * (using FTW_SKIP_SUBTREE)*/
#define _GNU_SOURCE

#include <ftw.h>        // encapsulates file-tree waklk functions such as opendir(), readdir(), struct dirent, etc.
// https://stackoverflow.com/a/29402705
// as the answerer points out, this has the advantage of handling scenario when files/dir are
// move mid-traversal
#include <stdio.h>      // fprint()
#include <unistd.h>     //getopt()
#include <string.h>     // strcmp()
#include <dirent.h>     //opendir(), readdir(), struct dirent, etc.
#include <sys/types.h>  // ino_t
#include <errno.h>      // errno
#include <sys/stat.h>   // stat
#include <time.h>       // localtime_r()
#include <stdlib.h>     // malloc() free()
#include <pwd.h>        // getpwuid()
#include <grp.h>        // getgrgid()

// DEBUG macro is used to turn on various debugging features
// Disable at the release version
#define DEBUG

#define MAXDIRCOUNT 256
/* POSIX.1 says each process has at least 20 file descriptors.
 * Three of those belong to the standard streams.
 * Here, we use a conservative estimate of 15 available;
 * assuming we use at most two for other uses in this program,
 * we should never run into any problems.
 * Most trees are shallower than that, so it is efficient.
 * Deeper trees are traversed fine, just a bit slower.
 * (Linux allows typically hundreds to thousands of open files,
 *  so you'll probably never see any issues even if you used
 *  a much higher value, say a couple of hundred, but
 *  15 is a safe, reasonable value.)
*/
#ifndef USE_FDS
#define USE_FDS 15  // the maximum number of directories that ftw() will hold open simultaneously
#endif

// for use to indicate which flag was used
static int iFlag;
static int RFlag;
static int lFlag;

#ifndef BUFFSIZE
#define BUFFSIZE 1024
#endif

// for printSubDir() [fn() in nftw()]
static char currSubDir[BUFFSIZE];


// Finds a specific string in a (portion of) str array
// used for finding special patterns '..', '.', '~' in argv[]
// search starts from strArr[beginArrPos] through strArr[endArrPos]
// note that this is a whole-string find, so searching for  "." for example will not return ".."
// return the index of first string matching the pattern
// return -1 upon not found
static int findArgument(char *pattern, char **strArr, int beginArrPos, int endArrPos);

// prints the directory passed in
// return errno (0= no error)
int print_directory_tree(const char *const dirpath);

// core function that prints the  info in a file/directory given
// parameters specified by nftw()
// https://linux.die.net/man/3/nftw
static int printFile(const char *filepath, const struct stat *info,
                     const int typeflag, struct FTW *pathinfo);

// prints the all the file/dir info in a dir given
// recursive call to itself to handle the '-R' flag
// modified based on: https://stackoverflow.com/a/8438663
static int printSubDir(const char *filepath, const struct stat *info,
                       const int typeflag, struct FTW *pathinfo);

// This is a non-standard system function on Solaris only
// Hence must be defined in UNIX (despite having to type out its name..)
// https://stackoverflow.com/a/9639381/5340330
char const *sperm(__mode_t mode);

// Boolean func to determine whether the filepath is a special directory
// special directory is defined as one of the three: '.', '..', or a hidden file (name beginning with ./.xx)
// assumes the filepath always starts with './xxx'
// return 1 for true; 0 for false
int isSpecialDir(const char *filePath);

// determine the current subdirectory that filepath is pointing to by counting the number of '/'s
// return the pos of the last '/'
// return 0 if last '/' is at 1, as filepath always begins with './'
size_t findCurrSubDirIdx(const char *filepath, size_t filepathLength);

int main(int argc, char *argv[]) {
    iFlag = 0;
    RFlag = 0;
    lFlag = 0;
    // dynamic str arr to store all the directory names
    char *dirsBuff[MAXDIRCOUNT];
    int index;
    int flagChar;
    int dirCount = 0;
    int ret;
    opterr = 0;

    // ------------------------used getopt to parse flags-------------------
    // https://www.gnu.org/software/libc/manual/html_node/Example-of-Getopt.html
    while ((flagChar = getopt(argc, argv, "iRl")) != -1)
        switch (flagChar) {
            case 'i':
                iFlag = 1;
                break;
            case 'R':
                RFlag = 1;
                break;
            case 'l':
                lFlag = 1;
                break;
            default:
                fprintf(stderr, "Usage: %s [-iRl] [directory...]\n", argv[0]);
                return EXIT_FAILURE;
        }

    // ----------------------the non-flag arguments------------------------
    // no directory argument was passed in: print current directory
    if (optind == argc)
        print_directory_tree(".");
    else {
        // Note: ls cmd always print . first, then .., then ~, (if they exist) then everything else
        // for this reason we first search for these three special cases
        if (findArgument(".", argv, optind, argc - 1) > 0)  // even tho 0 indicates a found, argv[0] is program name
            print_directory_tree(".");
        if (findArgument("..", argv, optind, argc - 1) > 0)  // even tho 0 indicates a found, argv[0] is program name
            print_directory_tree("..");
        if (findArgument("~", argv, optind, argc - 1) > 0)  // even tho 0 indicates a found, argv[0] is program name
            print_directory_tree("~");

        // print the rest of the directories
        for (index = optind; index < argc; index++) {
            if (strcmp(argv[index], ".") && strcmp(argv[index], "~")) {
                if ((ret = print_directory_tree(argv[index])) != 0)
#ifdef DEBUG
                    fprintf(stderr, "Printing directory '%s' error: %d", argv[index], ret);
#endif
            }
        }
    }

    return EXIT_SUCCESS;
}

// Finds a specific string in a (portion of) str array
// used for finding special patterns '..', '.', '~' in argv[]
// search starts from strArr[beginArrPos] through strArr[endArrPos]
// note that this is a whole-string find, so searching for  "." for example will not return ".."
// return the index of first string matching the pattern
// return -1 upon not found
static int findArgument(char *pattern, char **strArr, int beginArrPos, int endArrPos) {
    int i;

    for (i = beginArrPos; i < endArrPos + 1; i++) {
        if (!strcmp(strArr[i], pattern) && strlen(pattern) == strlen(strArr[i])) {
            return i;
        }
    }
    return -1;
}

// recursively print_dir_tree
// modified based on: https://stackoverflow.com/a/8438663
static int printSubDir(const char *filepath, const struct stat *info,
                       const int typeflag, struct FTW *pathinfo) {
    // ignore . and .. and hidden files
    if (isSpecialDir(filepath))
        return 0;

    /*
     * else if (typeflag == FTW_D || typeflag == FTW_DP)
     * print_directory_tree(filepath);
     * recursively calling this would obviously be the most elegant solution, however due to the fact that nftw() does
     * not support recursive call that modifies . and .. (even with FTW_CHDIR flag enabled), unfortunately we must
     * create another logic to handle this.
     */
    size_t subDirIdx;      // the index of the last char of the dir within filepath
    char newSubDir[BUFFSIZE];
    int errcode=0;

    subDirIdx = findCurrSubDirIdx(filepath, strlen(filepath));

    if(typeflag != FTW_D && typeflag != FTW_DP && !subDirIdx)     // not a dir nor not a file in subdir
        return 0;

    // modify newSubStr if its a (file in) subdir
    if(subDirIdx){
        // be aware of buffer overflow
        if(subDirIdx<BUFFSIZE){
            strncpy(newSubDir, filepath, subDirIdx);
            /*Warning: If there is no null byte among the first n
             * bytes of src, the string placed in dest will not be null-terminated.
             * http://man7.org/linux/man-pages/man3/strcpy.3.html
             * */
            if(subDirIdx<sizeof(newSubDir)-1)
                newSubDir[subDirIdx]='\0';
        }else{
#ifdef DEBUG
            fprintf(stderr, "printSubDir: subDirIdx=%lu overflow currSubDir[%d] for '%s'\n", (unsigned long)subDirIdx,BUFFSIZE,filepath);
#endif
            return 0;       // the show must go on!
        }
    }

    // new subdir
    if ((typeflag == FTW_D || typeflag == FTW_DP) && strcmp(filepath, currSubDir)) {
        // update global variable
        strcpy(currSubDir,filepath);
        printf("\n%s:\n",currSubDir);
    }

    errcode=printFile(filepath, info, typeflag, pathinfo);
#ifdef DEBUG
    if(errcode)
        fprintf(stderr,"printFile error in printSubDir '%s': %s\n",filepath,strerror(errcode));
#endif

    return 0;
}


// core function that prints the  info in a file/directory given
// parameters specified by nftw()
// https://linux.die.net/man/3/nftw
static int printFile(const char *filepath, const struct stat *info,
                     const int typeflag, struct FTW *pathinfo) {
    // get size of file/dir
    const double bytes;
    struct tm mtime;
    struct passwd *pwd;
    struct group *grp;

    // ignore . and .. and hidden files
    if (isSpecialDir(filepath))
        return 0;

    // -i: begin the line with printing the inode #
    if (iFlag) {
        printf("%-6lu ", info->st_ino);    // unsigned long st_into
    }

    // -l: printing additional info before the filename
    // format: r/w/e permission, # of hard links, owner name, group name, size in bytes, lost modified timestamp
    if (lFlag) {
        // Print out type, permissions, and number of links
        if (typeflag == FTW_D || typeflag == FTW_DP)       // valid dir (FTW_DP = FTW_DEPTH was specified in flags)
            printf("d");
        else
            printf("-");
        printf("%-10.10s", sperm(info->st_mode));
        printf("%-4lu", info->st_nlink);

        // Print out owner's and group's name if it is found using getpwuid()
        // http://pubs.opengroup.org/onlinepubs/009695399/functions/getpwuid.html
        // http://pubs.opengroup.org/onlinepubs/009695399/functions/getgrgid.html
        if ((pwd = getpwuid(info->st_uid)) != NULL)
            printf(" %-8.8s", pwd->pw_name);
        else
            printf(" %-8d", info->st_uid);
        if ((grp = getgrgid(info->st_gid)) != NULL)
            printf(" %-8.8s", grp->gr_name);
        else
            printf(" %-8d", info->st_gid);


        // print out file/dir size
        bytes == (double) info->st_size; /* Not exact if large! */
//        if (bytes >= 1099511627776.0)
//            printf(" %9.3f TiB", bytes / 1099511627776.0);
//        else if (bytes >= 1073741824.0)
//            printf(" %9.3f GiB", bytes / 1073741824.0);
//        else if (bytes >= 1048576.0)
//            printf(" %9.3f MiB", bytes / 1048576.0);
//        else if (bytes >= 1024.0)
//            printf(" %9.3f KiB", bytes / 1024.0);
//        else
        printf(" %9.0f ", bytes);

        // get last modified timestamp of file/dir
        localtime_r(&(info->st_mtime), &mtime);
        printf("%04d-%02d-%02d %02d:%02d:%02d",
               mtime.tm_year + 1900, mtime.tm_mon + 1, mtime.tm_mday,
               mtime.tm_hour, mtime.tm_min, mtime.tm_sec);
    }

    // name printing
    // note the filepath+2 is to neglect the first two char, which are './'

    // handle symbolic link
    if (typeflag == FTW_SL) {
        if (lFlag) {
            char *target;
            size_t maxlen = 1023;
            ssize_t len;

            while (1) {
                target = malloc(maxlen + 1);
                if (target == NULL)
                    return ENOMEM;      // throw errno 'out of memory'

                len = readlink(filepath, target, maxlen);
                if (len == (ssize_t) -1) {
                    // save errno, free mem, and toss errno
                    const int saved_errno = errno;
                    free(target);
                    return saved_errno;
                }
                // in case of ssize_t overflow, a problem in readlink() Issue 6:
                /*
                 * In this function it is possible for the return value to exceed the
                 * range of the type ssize_t (since size_t has a larger range of positive
                 * values than ssize_t). A sentence restricting the size of the size_t
                 * object is added to the description to resolve this conflict.
                 * http://pubs.opengroup.org/onlinepubs/009695399/functions/readlink.html
                 */
                if (len >= (ssize_t) maxlen) {
                    free(target);
                    maxlen += 1024;
                    continue;
                }

                target[len] = '\0';
                break;
            }

            printf(" %s -> %s\n", filepath + 2, target);
            free(target);
        } else
            printf(" %s\n", filepath + 2);
    }
        // filepath is a symbolic link pointing to a nonexistent file.
    else if (typeflag == FTW_SLN)
        printf(" %s (dangling symlink)\n", filepath + 2);
    else if (typeflag == FTW_F)     // regular file
        printf(" %s\n", filepath + 2);
    else if (typeflag == FTW_D || typeflag == FTW_DP) {      // valid dir (FTW_DP = FTW_DEPTH was specified in flags)
        printf(" %s\n", filepath + 2);
        if(currSubDir[0]=='\0')     // global var will be blank when not in printSubDir()
            return FTW_SKIP_SUBTREE;
    } else if (typeflag == FTW_DNR)       // unreadable dir
        printf(" %s/ (unreadable)\n", filepath + 2);
    else
        printf(" %s (unknown)\n", filepath + 2);

    return 0;
}

// obsolete
//    // Declare a stat structure (e.g., buf), and then use this buf and dirPtr->d_name to call stat() or lstat() as appropriate.
//    struct stat statBuf;
//    errno = 0;
//
//    // lstat() retrieves the info about soft link instead of info about the file soft link is pointing to
//    if (lstat(direntPtr->d_name, &statBuf) == -1) {
//#ifdef DEBUG
//        fprintf(stderr, "stat: cannot access '%s'", direntPtr->d_name);
//        perror("");
//#endif
//        return;
//    }
//    // -i: begin the line with printing the inode #
//    if (iFlag) {
//        printf("%-7lu", statBuf.st_ino);    // unsigned long st_into
//    }
//
//    // print out name of dir/file
//    printf("%s\n",direntPtr->d_name);
//
////
////    // -l: verbose mode printing all the details
////    if (lFlag)
////
////        // Print out type, permissions, and number of links
////        printf("%10.10s", sperm(statBuf.st_mode));
////    printf("%4d", statBuf.st_nlink);
////
////
////    /* Print out owner's name if it is found using getpwuid(). */
////    if ((pwd = getpwuid(statBuf.st_uid)) != NULL)
////        printf(" %-8.8s", pwd->pw_name);
////    else
////        printf(" %-8d", statBuf.st_uid);
////
////
////    /* Print out group name if it is found using getgrgid(). */
////    if ((grp = getgrgid(statBuf.st_gid)) != NULL)
////        printf(" %-8.8s", grp->gr_name);
////    else
////        printf(" %-8d", statBuf.st_gid);
////
////
////    /* Print size of file. */
////    printf(" %9jd", (intmax_t) statBuf.st_size);
////
////
////    tm = localtime(&statBuf.st_mtime);
////
////
////    /* Get localized date string. */
////    strftime(datestring, sizeof(datestring), nl_langinfo(D_T_FMT), tm);
////
////
////    printf(" %s %s\n", datestring, dirPtr->d_name);

// prints the directory passed in
int print_directory_tree(const char *const dirpath) {
    int result;

    // handle invalid directory path
    if (dirpath == NULL || *dirpath == '\0')
        return errno = EINVAL;

    // clear currSubDir
    memset(&currSubDir, 0, sizeof currSubDir);

    printf("%s:\n", dirpath);
    result = nftw(dirpath, printFile, USE_FDS, FTW_PHYS | FTW_ACTIONRETVAL | FTW_CHDIR);     /* set FTW_PHYS: Do not follow sym links
*                                                                since we are doing it manually*/
    puts("");

    if (result > 0) {       // handle error
        errno = result;
    }
    // additionally, handle -R flag
    else if (RFlag) {
        result = nftw(dirpath, printSubDir, USE_FDS,
                      FTW_PHYS | FTW_CHDIR);      /* printSubDir will recursively call print_directory_tree() for each dir
 * set FTW_PHYS: Do not follow sym links since we are doing it manually
 * set FTW_CHDIR: do a chdir(2) to each directory before handling its contents */
        errno = result;     // errno=0 if no error
    }

    return errno;
}

/*
// obsolete
//// prints the all the file/dir info in a dir given
//// recursive call to itself to handle the '-R' flag
//// modified based on: https://stackoverflow.com/a/8438663
//static void printDir(char *dirName) {
//    DIR *dir;
//    struct dirent *direntPtr;
//
//    // 1. Open a directory using opendir().
//    errno = 0;
//    if ((dir = opendir(dirName)) == NULL) {
//        fprintf(stderr, "ls: cannot access '%s'", dirName);
//        perror("");
//        return;
//    }
//
//    // print the current directories
//    // control logic based on stat manual example here:
//    // http://pubs.opengroup.org/onlinepubs/009695399/functions/stat.html
//    errno=0;
//    while ((direntPtr = readdir(dir)) != NULL) {
//        // print file/dir's information if not (hidden || is .. || is .)
//        if(direntPtr->d_type == DT_DIR && (direntPtr->d_name)[0]!= '.')
//            printFile(direntPtr);
//    }
//
//    if (errno != 0){
//#ifdef DEBUG
//        fprintf(stderr,"error reading directory '%s'",dirName);
//        perror("");
//#endif
//        closedir(dir);
//        return;
//    }
//
//
//    // 5. closedir(), once you have finished reading a directory it needs to be closed using closedir().
//    closedir(dir);
//}
 */

// This is a non-standard system function on Solaris only
// Hence must be defined in UNIX (despite having to type out its name..)
// https://stackoverflow.com/a/9639381/5340330
char const *sperm(__mode_t mode) {
    // the bits in st_mode are interpreted as such:
    // https://stackoverflow.com/a/35375259
    static char local_buff[16] = {0};
    int i = 0;
    // user permissions
    if ((mode & S_IRUSR) == S_IRUSR) local_buff[i] = 'r';
    else local_buff[i] = '-';
    i++;
    if ((mode & S_IWUSR) == S_IWUSR) local_buff[i] = 'w';
    else local_buff[i] = '-';
    i++;
    if ((mode & S_IXUSR) == S_IXUSR) local_buff[i] = 'x';
    else local_buff[i] = '-';
    i++;
    // group permissions
    if ((mode & S_IRGRP) == S_IRGRP) local_buff[i] = 'r';
    else local_buff[i] = '-';
    i++;
    if ((mode & S_IWGRP) == S_IWGRP) local_buff[i] = 'w';
    else local_buff[i] = '-';
    i++;
    if ((mode & S_IXGRP) == S_IXGRP) local_buff[i] = 'x';
    else local_buff[i] = '-';
    i++;
    // other permissions
    if ((mode & S_IROTH) == S_IROTH) local_buff[i] = 'r';
    else local_buff[i] = '-';
    i++;
    if ((mode & S_IWOTH) == S_IWOTH) local_buff[i] = 'w';
    else local_buff[i] = '-';
    i++;
    if ((mode & S_IXOTH) == S_IXOTH) local_buff[i] = 'x';
    else local_buff[i] = '-';
    return local_buff;
}

// Boolean func to determine whether the filepath is a special directory
// special directory is defined as one of the three: '.', '..', or a hidden file (name beginning with ./.xx)
// assumes the filepath always starts with './xxx'
// return 1 for true; 0 for false
int isSpecialDir(const char *filePath) {
    if (strlen(filePath) <= 2 || filePath[2] == '.')        // either . or ..
        return 1;
    else return 0;
}

// determine the current subdirectory that filepath is pointing to by counting the number of '/'s
// return the pos of the last '/'
// return 0 if last '/' is at 1, as filepath always begins with './'
size_t findCurrSubDirIdx(const char *filepath, size_t filepathLength) {
    size_t lastSlashIdx = 0;
    int i;
    for (i = 0; i < filepathLength && filepath[i] != '\0'; i++) {
        if (filepath[i] == '/')
            lastSlashIdx = i;
    }
    if (lastSlashIdx == 1)
        lastSlashIdx = 0;
    return lastSlashIdx;
}

