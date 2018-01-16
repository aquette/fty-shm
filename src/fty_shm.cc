/*  =========================================================================
    fty_shm - FTY metric sharing functions

    Copyright (C) 2018 Eaton

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
    =========================================================================
*/

/*
@header
    fty_shm - FTY metric sharing functions
@discuss
@end
*/

#include <unordered_set>
#include <algorithm>
#include <dirent.h>

#include "fty_shm_classes.h"

#define DEFAULT_SHM_DIR       "/var/run/fty-shm-1"
#define METRIC_SUFFIX ".metric"
#define SUFFIX_LEN    (sizeof(METRIC_SUFFIX) - 1)
// DEFAULT_SHM_DIR "/" FILENAME NUL
#define PATH_BUF_SIZE (sizeof(DEFAULT_SHM_DIR) + NAME_MAX + 1)

// The first 11 bytes of each file are the ttl in 10 decimal digits, followed
// by \n.  This is a compromise between machine and human readability
#define TTL_FMT "%010d\n"
#define TTL_LEN 11

// This is only changed by the selftest code
static const char *shm_dir = DEFAULT_SHM_DIR;

static int validate_names(const std::string &asset, const std::string &metric)
{
    if (asset.length() + strlen(":") + metric.length() + SUFFIX_LEN > NAME_MAX) {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (asset.find_first_of("/:", 0, 2) != asset.npos ||
            metric.find_first_of("/:", 0, 2) != metric.npos) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static ssize_t write_buf(int fd, const char *buf, size_t len)
{
    size_t written = 0;
    ssize_t ret;

    while (written < len) {
        if ((ret = write(fd, buf + written, len - written)) < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        written += ret;
    }
    return written;
}

static ssize_t read_buf(int fd, char *buf, size_t len)
{
    size_t done = 0;
    ssize_t ret;

    while (done < len) {
        if ((ret = read(fd, buf + done, len - done)) < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        done += ret;
    }
    return done;
}

static ssize_t read_buf(int fd, std::string &buf, size_t len)
{
    return read_buf(fd, &buf.front(), len);
}

// Write ttl and value to filename.tmp and atomically replace filename
static int write_value(const char *filename, const char *value, int ttl)
{
    int fd;
    char ttl_str[TTL_LEN + 1], tmp[PATH_BUF_SIZE];

    snprintf(tmp, sizeof(tmp), "%s/tmp.XXXXXX", shm_dir);
    if ((fd = mkostemp(tmp, O_CLOEXEC)) < 0)
        return -1;
    if (ttl < 0)
        ttl = 0;
    snprintf(ttl_str, sizeof(ttl_str), TTL_FMT, ttl);
    // XXX: We can combine this using writev()
    if (write_buf(fd, ttl_str, TTL_LEN) < 0 ||
                write_buf(fd, value, strlen(value)) < 0) {
        close(fd);
        goto out_unlink;
    }
    if (close(fd) < 0)
        goto out_unlink;
    if (rename(tmp, filename) < 0)
        goto out_unlink;
    return 0;

out_unlink:
    unlink(tmp);
    return -1;
}

bool alloc_str(char * &str, size_t len)
{
    char *tmp = (char *)malloc(len + 1);
    if (!tmp)
        return false;
    str = tmp;
    return true;
}

bool alloc_str(std::string &str, size_t len)
{
    str.resize(len);
    return true;
}

void free_str(char * &str)
{
    free(str);
    str = NULL;
}

void free_str(std::string &str)
{
    // NO-OP
}

// XXX: The error codes are somewhat arbitrary
template<typename T>
static int read_value(const char *filename, T &value)
{
    int fd;
    struct stat st;
    size_t size;
    char ttl_str[TTL_LEN], *err;
    T buf;
    time_t now, ttl;
    int ret = -1;

    if ((fd = open(filename, O_RDONLY | O_CLOEXEC)) < 0)
        return ret;
    if (fstat(fd, &st) < 0)
        goto out;
    if (st.st_size < TTL_LEN) {
        errno = EIO;
        goto out;
    }
    if (read_buf(fd, ttl_str, TTL_LEN) < 0)
        goto out;
    // Delete the '\n'
    ttl_str[TTL_LEN - 1] = '\0';
    ttl = strtol(ttl_str, &err, 10);
    if (err != ttl_str + TTL_LEN - 1) {
        errno = ERANGE;
        goto out;
    }
    now = time(NULL);
    if (now - st.st_mtime > ttl) {
        errno = ESTALE;
        goto out;
    }
    size = st.st_size - TTL_LEN;
    if (!alloc_str(buf, size))
        goto out;
    buf[size] = '\0';
    if (read_buf(fd, buf, size) < 0) {
        free_str(buf);
        goto out;
    }
    value = buf;
    ret = 0;
out:
    close(fd);
    return ret;
}

int fty_shm_write_metric(const char *asset, const char *metric, const char *value, int ttl)
{
    char filename[PATH_BUF_SIZE];

    if (validate_names(asset, metric) < 0)
        return -1;
    sprintf(filename, "%s/%s:%s" METRIC_SUFFIX, shm_dir, asset, metric);
    return write_value(filename, value, ttl);
}

int fty_shm_read_metric(const char *asset, const char *metric, char **value)
{
    char filename[PATH_BUF_SIZE];

    if (validate_names(asset, metric) < 0)
        return -1;
    sprintf(filename, "%s/%s:%s" METRIC_SUFFIX, shm_dir, asset, metric);
    return read_value(filename, *value);
}

int fty::shm::read_metric(const std::string &asset, const std::string &metric, std::string &value)
{
    char filename[PATH_BUF_SIZE];

    if (validate_names(asset, metric) < 0)
        return -1;
    sprintf(filename, "%s/%s:%s" METRIC_SUFFIX, shm_dir, asset.c_str(), metric.c_str());
    return read_value(filename, value);
}

int fty::shm::find_assets(Assets &assets)
{
    DIR *dir;
    struct dirent *de;
    std::unordered_set<std::string> seen;

    if (!(dir = opendir(shm_dir)))
        return -1;

    // TODO: Remember the number of items from last time and reserve it
    assets.clear();
    while ((de = readdir(dir))) {
        char *delim = strchr(de->d_name, ':');
        if (!delim)
            // Malformed filename
            continue;
        *delim = '\0';
        if (!seen.insert(de->d_name).second)
            continue;
        assets.push_back(de->d_name);
    }
    closedir(dir);
    return 0;
}

int fty::shm::read_asset_metrics(const std::string &asset, Metrics &metrics)
{
    DIR *dir;
    struct dirent *de;
    int err = -1;

    if (!(dir = opendir(shm_dir)))
        return -1;

    metrics.clear();
    while ((de = readdir(dir))) {
        const char *delim = strchr(de->d_name, ':');
        size_t len = strlen(de->d_name);
        if (!delim || len < SUFFIX_LEN)
            // Malformed filename
            continue;
        size_t asset_len = delim - de->d_name;
        size_t metric_len = len - asset_len - strlen(":") - SUFFIX_LEN;
        if (strncmp(de->d_name + len - SUFFIX_LEN, METRIC_SUFFIX, SUFFIX_LEN) != 0)
            // Not a metric
            continue;
	if (std::string(de->d_name, asset_len) != asset)
            continue;
        std::string value;
        char filename[PATH_BUF_SIZE];
        sprintf(filename, "%s/%s", shm_dir, de->d_name);
        if (read_value(filename, value) < 0)
            continue;
        err = 0;
        metrics.emplace(std::string(delim + 1, metric_len), value);
    }
    closedir(dir);
    return err;
}

//  --------------------------------------------------------------------------
//  Self test of this class

// Version of assert() that prints the errno value for easier debugging
#define check_err(expr) do {                                          \
    if ((expr) < 0) {                                                 \
        fprintf(stderr, __FILE__ ":%d: Assertion `%s' failed (%s)\n", \
            __LINE__, #expr, strerror(errno));                        \
        abort();                                                      \
    }                                                                 \
} while (0)

void
fty_shm_test (bool verbose)
{
    char *value = NULL;
    std::string string;
    const char *asset1 = "test_asset_1", *asset2 = "test_asset_2";
    const char *metric1 = "test_metric_1", *metric2 = "test_metric_2";
    const char *value1 = "hello world", *value2 = "This is\na metric";

    printf (" * fty_shm: ");

    // Check for invalid characters
    assert(fty_shm_write_metric("invalid/asset", metric1, value1, 0) < 0);
    assert(fty_shm_read_metric ("invalid/asset", metric1, &value) < 0);
    assert(!value);
    assert(fty_shm_write_metric(asset1, "invalid:metric", value1, 0) < 0);
    assert(fty_shm_read_metric (asset1, "invalid:metric", &value) < 0);
    assert(!value);

    // Check for too long asset or metric name
    char *name2long = (char *)malloc(NAME_MAX + 10);
    assert(name2long);
    memset(name2long, 'A', NAME_MAX + 9);
    name2long[NAME_MAX + 9] = '\0';
    assert(fty_shm_write_metric(name2long, metric1, value1, 300) < 0);
    assert(fty_shm_read_metric (name2long, metric1, &value) < 0);
    assert(!value);
    assert(fty_shm_write_metric(asset1, name2long, value1, 300) < 0);
    assert(fty_shm_read_metric (asset1, name2long, &value) < 0);
    assert(!value);
    free(name2long);

    shm_dir = "src/selftest-rw";
    assert(strlen(shm_dir) <= strlen(DEFAULT_SHM_DIR));
    check_err(access(shm_dir, X_OK | W_OK));
    // The buildsystem does not delete this for some reason
    system("rm -f src/selftest-rw/*");

    // Check that the storage is empty
    fty::shm::Assets assets;
    fty::shm::find_assets(assets);
    assert(assets.size() == 0);

    // Write and read back a metric
    check_err(fty_shm_write_metric(asset1, metric1, value1, 0));
    check_err(fty_shm_read_metric (asset1, metric1, &value));
    assert(value);
    assert(streq(value, value1));
    zstr_free(&value);

    // Update a metric (C++)
    check_err(fty::shm::write_metric(asset1, metric1, value2, 0));
    check_err(fty::shm::read_metric (asset1, metric1, string));
    assert(string == value2);

    // List assets
    check_err(fty_shm_write_metric(asset1, metric2, value1, 0));
    check_err(fty_shm_write_metric(asset2, metric1, value1, 0));
    fty::shm::find_assets(assets);
    assert(assets.size() == 2);
    assert(std::find(assets.begin(), assets.end(), asset1) != assets.end());
    assert(std::find(assets.begin(), assets.end(), asset2) != assets.end());

    // Load all metrics for an asset
    fty::shm::Metrics metrics;
    check_err(fty::shm::read_asset_metrics(asset1, metrics));
    assert(metrics.size() == 2);
    assert(metrics[metric1] == value2);
    assert(metrics[metric2] == value1);

    // TTL OK
    check_err(fty_shm_write_metric(asset2, metric1, value2, INT_MAX));
    check_err(fty_shm_read_metric (asset2, metric1, &value));
    assert(value);
    assert(streq(value, value2));
    zstr_free(&value);

    // TTL expired
    check_err(fty_shm_write_metric(asset2, metric1, value2, 1));
    sleep(2);
    assert(fty_shm_read_metric (asset2, metric1, &value) < 0);
    assert(!value);

    printf ("OK\n");
}
