// -*- mode: c; tab-width: 4; indent-tabs-mode: nil -*-

#include <gme/gme.h>
#include <sndfile.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <libgen.h>
#include <sys/mman.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>

#define IO_INPUT SFM_READ
#define IO_OUTPUT SFM_WRITE

struct options_s
{
    char *input;
    char *output;
    int track;
    int samplerate;
    int duration;
    int fadeout;
    double pan;
    bool help;
};

static struct option optspec[] = {
    {"output", required_argument, 0, 'o'},
    {"track", required_argument, 0, 't'},
    {"samplerate", required_argument, 0, 's'},
    {"duration", required_argument, 0, 'd'},
    {"fadeout", required_argument, 0, 'f'},
    {"pan", required_argument, 0, 'p'},
    {"help", no_argument, 0, 'h'},
    {0, 0, 0, 0}
};

bool init_options(int argc, char * const argv[], struct options_s *opts);
void free_options(struct options_s *opts);
int open_file(const char *path, int mode);
int open_enumerated_file(const char *path, int mode, int index);
bool buffer_file(int fd, void **buf, size_t *bufsiz);
bool play(Music_Emu *emu, int track, int duration, int fadeout, double pan, void (*sample_handler)(short *samplebuf, size_t samplebufsiz, void *ctx), void *ctx)
;
void write_samples(short *samplebuf, size_t samplebufsiz, void *ctx);
void copy_metadata(const Music_Emu *emu, int track, SNDFILE *sndfile);
void print_error(const char *message);
void print_system_error(const char *message);
void print_sndfile_error(const char *message, SNDFILE *sndfile);
void print_help(int argc, char * const argv[]);

bool init_options(int argc, char * const argv[], struct options_s *opts)
{

    memset(opts, 0, sizeof(struct options_s));

    opts->input = NULL;
    opts->output = NULL;
    opts->track = -1;
    opts->samplerate = 44100;
    opts->duration = 180;
    opts->fadeout = 5;
    opts->pan = 0.0;
    opts->help = false;

    char opt;
    while ((opt = getopt_long(argc, argv, "o:t:s:d:f:p:h", optspec, 0)) != -1)
    {
        switch (opt)
        {
            case 'o':
                opts->output = strdup(optarg);
                break;

            case 't':
                opts->track = atol(optarg);
                break;

            case 's':
                opts->samplerate = atol(optarg);
                break;

            case 'd':
                opts->duration = atol(optarg);
                break;

            case 'f':
                opts->fadeout = atol(optarg);
                break;

            case 'p':
                opts->pan = atof(optarg);
                break;

            case 'h':
                opts->help = true;
                break;

            case '?':
                return false;
                break;

            case ':':
                return false;
                break;

        }
    }

    if (optind < argc)
    {
        opts->input = strdup(argv[optind]);
    }

    return true;

}

void free_options(struct options_s *opts)
{
    free(opts->input);
    free(opts->output);
}

int open_file(const char *path, int mode)
{
    if (mode != IO_INPUT && mode != IO_OUTPUT)
    {
        return -1;
    }

    if (path != NULL && strcmp("-", path) != 0)
    {
        return open(path, mode == IO_INPUT ? O_RDONLY : (O_WRONLY | O_CREAT | O_TRUNC), S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);   
    }
    else
    {
        return mode == IO_INPUT ? STDIN_FILENO : STDOUT_FILENO;
    }
}

int open_enumerated_file(const char *path, int mode, int index)
{
    char newpath[PATH_MAX] = {0};

    if (strstr(path, "%d") == NULL)
    {
        snprintf(newpath, PATH_MAX, "%d-%s", index, path);
    }
    else
    {
        snprintf(newpath, PATH_MAX, path, index);
    }

    return open_file(newpath, mode);
}

void close_file(int fd)
{
    if (fd != STDIN_FILENO && fd != STDOUT_FILENO)
    {
        close(fd);
    }
}

bool buffer_file(int fd, void **buf, size_t *bufsiz)
{
    ssize_t nread;
    size_t tmpbufsiz, navail;
    void *tmpbuf, *p;

    tmpbufsiz = navail = 1024;
    tmpbuf = p = malloc(tmpbufsiz);

    while ((nread = read(fd, p, navail)) > 0)
    {
        navail = navail - nread;

        if (navail == 0)
        {
            navail = tmpbufsiz;
            tmpbufsiz = tmpbufsiz * 2;
            tmpbuf = realloc(tmpbuf, tmpbufsiz);
        }

        p = tmpbuf + tmpbufsiz - navail;
    }

    if (nread == -1)
    {
        free(tmpbuf);
        return false;
    }

    *buf = tmpbuf;
    *bufsiz = (p - tmpbuf);

    return true;
}

bool play(Music_Emu *emu, int track, int duration, int fadeout, double pan, void (*sample_handler)(short *samplebuf, size_t samplebufsiz, void *ctx), void *ctx)
{
    short samplebuf[1024];
    int pos;
    gme_err_t emu_err;

    gme_start_track(emu, track);
    gme_seek(emu, 0);
    gme_set_fade(emu, (duration - fadeout) * 1000L);
    gme_set_stereo_depth(emu, pan);

    while ((pos = gme_tell(emu)) < duration * 1000L)
    {
        memset(samplebuf, 1024, 0);
        emu_err = gme_play(emu, 1024, samplebuf);
        
        if (emu_err != NULL)
        {
            print_error(emu_err);
            return false;
        }

        sample_handler(samplebuf, 1024, ctx); // TODO: 1024 is not necessarily actual length of data in buffer
    }

    return true;
}

void write_samples(short *samplebuf, size_t samplebufsiz, void *ctx)
{
    SNDFILE *sndfile = (SNDFILE *) ctx;

    sf_write_short(sndfile, samplebuf, samplebufsiz);
}

void copy_metadata(const Music_Emu *emu, int track, SNDFILE *sndfile)
{
    gme_info_t *info = NULL;

    gme_track_info(emu, &info, track);

    sf_set_string(sndfile, SF_STR_TITLE, info->song);
    sf_set_string(sndfile, SF_STR_ALBUM, info->game);
    sf_set_string(sndfile, SF_STR_ARTIST, info->author);
    sf_set_string(sndfile, SF_STR_COPYRIGHT, info->copyright);
    sf_set_string(sndfile, SF_STR_COMMENT, info->comment);

    gme_free_info(info);
}

void print_error(const char *message)
{
    fprintf(stderr, "error: %s\n", message);
}

void print_system_error(const char *message)
{
    fprintf(stderr, "error: %s", message);

    if (errno != 0)
    {
        fprintf(stderr, " (%s)", strerror(errno));
    }

    fprintf(stderr, "\n");
}

void print_sndfile_error(const char *message, SNDFILE *sndfile)
{
    fprintf(stderr, "error: %s", message);

    if (sf_error(sndfile) != SF_ERR_NO_ERROR)
    {
        fprintf(stderr, " (%s)", sf_strerror(sndfile));
    }

    fprintf(stderr, "\n");
}

void print_help(int argc, char * const argv[])
{
    struct option *option = optspec;

    fprintf(stderr, "usage: %s: <options> <input>\n\n", basename(argv[0]));

    do
    {
        fprintf(stderr, "    -%c --%s", option->val, option->name);

        if (option->has_arg == required_argument)
        {
            fprintf(stderr, " %s", option->name);
        }
        else if (option->has_arg == optional_argument)
        {
            fprintf(stderr, " [%s]", option->name);
        }

        fprintf(stderr, "\n");

    } while ((++option)->name != NULL);
}

int main(int argc, char * const argv[])
{
    int rc;
    struct options_s opts;

    rc = EXIT_FAILURE;

    if (init_options(argc, argv, &opts)) {

        if (opts.help)
        {
            print_help(argc, argv);
            rc = EXIT_SUCCESS;
        }
        else
        {
            int input = open_file(opts.input, IO_INPUT);

            if (input != -1)
            {
                void *buf;
                size_t bufsiz;

                if (buffer_file(input, &buf, &bufsiz))
                {
                    Music_Emu *emu;
                    gme_err_t emu_err = gme_open_data(buf, bufsiz, &emu, opts.samplerate);
                    
                    if (emu_err == NULL)
                    {
                        int ntracks = gme_track_count(emu);
                        int track = 0, ntracks_played;

                        if (opts.track != -1)
                        {
                            track = opts.track;
                            ntracks = 1;
                        }
                        
                        for (ntracks_played = 0; ntracks_played < ntracks; ++ntracks_played, ++track)
                        {
                            int output = open_enumerated_file(opts.output, IO_OUTPUT, track);

                            if (output != -1)
                            {
                                SF_INFO sfinfo;
                                memset(&sfinfo, sizeof(SF_INFO), 0);
                                sfinfo.samplerate = opts.samplerate;
                                sfinfo.channels = 2;
                                sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

                                SNDFILE *sndfile = sf_open_fd(output, IO_OUTPUT, &sfinfo, 0);

                                if (sndfile != NULL)
                                {
                                    play(emu, track, opts.duration, opts.fadeout, opts.pan, write_samples, sndfile);
                                    copy_metadata(emu, track, sndfile);
                                    
                                    sf_close(sndfile);
                                }
                                else
                                {
                                    print_sndfile_error("unable to create sound file handle", sndfile);
                                }

                                close_file(output);
                                
                            }
                            else
                            {
                                print_system_error("unable to open output file for writing");
                            }

                        }

                        gme_delete(emu);
                    }
                    else
                    {
                        print_error(emu_err);
                    }

                    free(buf);
                }
                else
                {
                    print_system_error("unable to buffer file");
                }

                close_file(input);
                
            }
            else
            {
                print_system_error("unable to open input file for reading");
            }

        }
        
        free_options(&opts);

    }
    else
    {
        print_error("invalid usage");
        print_help(argc, argv);
    }

    return rc;
}