/*
    Copyright (C) 2014 CSP Innovazione nelle ICT s.c.a r.l. (http://rd.csp.it/)

    Copyright (C) 2014, 2015 Matthias P. Braendli (http://opendigitalradio.org)

    Copyright (C) 2015, 2016, 2017, 2018 Stefan Pöschel (http://opendigitalradio.org)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
/*!
    \file sls.h
    \brief Slideshow related code

    \author Sergio Sagliocco <sergio.sagliocco@csp.it>
    \author Matthias P. Braendli <matthias@mpb.li>
    \author Stefan Pöschel <odr@basicmaster.de>
*/

#ifndef SLS_H_
#define SLS_H_

#include "common.h"
#include "pad_common.h"

#if HAVE_MAGICKWAND
#  if HAVE_MAGICKWAND_LEGACY
#    include <wand/magick_wand.h>
#  else
#    include <MagickWand/MagickWand.h>
#  endif
#endif

#include <dirent.h>
#include <sys/stat.h>
#include <deque>
#include <fstream>
#include <iostream>
#include <list>
#include <algorithm>


// --- MSCDG -----------------------------------------------------------------
struct MSCDG {
    // MSC Data Group Header (extension field not supported)
    unsigned char extflag;      //  1 bit
    unsigned char crcflag;      //  1 bit
    unsigned char segflag;      //  1 bit
    unsigned char accflag;      //  1 bit
    unsigned char dgtype;       //  4 bits
    unsigned char cindex;       //  4 bits
    unsigned char rindex;       //  4 bits
    /// Session header - Segment field
    unsigned char last;         //  1 bit
    unsigned short int segnum;  // 16 bits
    // Session header - User access field
    unsigned char rfa;          //  3 bits
    unsigned char tidflag;      //  1 bit
    unsigned char lenid;        //  4 bits - Fixed to value 2 in this implemntation
    unsigned short int tid;     // 16 bits
    // MSC data group data field
    //  Mot Segmentation header
    unsigned char rcount;       //  3 bits
    unsigned short int seglen;  // 13 bits
    // Mot segment
    const uint8_t* segdata;
    // MSC data group CRC
    unsigned short int crc;     // 16 bits
};


// --- slide_metadata_t -----------------------------------------------------------------
/*! Between collection of slides and transmission, the slide data is saved
 * in this structure.
 */
struct slide_metadata_t {
    // complete path to slide
    std::string filepath;

    // index, values from 0 to MAXSLIDEID, rolls over
    int fidx;

    // This is used to define the order in which several discovered
    // slides are transmitted
    bool operator<(const slide_metadata_t& other) const {
        return this->fidx < other.fidx;
    }
};


// --- fingerprint_t -----------------------------------------------------------------
/*! A simple fingerprint for each slide transmitted.
 * Allows us to reuse the same fidx if the same slide
 * is transmitted more than once.
 */
struct fingerprint_t {
    // file name
    std::string s_name;
    // file size, in bytes
    off_t s_size;
    // time of last modification
    unsigned long s_mtime;

    // assigned fidx, -1 means invalid
    int fidx;

    /*! The comparison is not done on fidx, only
     * on the file-specific data
     */
    bool operator==(const fingerprint_t& other) const {
        return (((s_name == other.s_name &&
                 s_size == other.s_size) &&
                s_mtime == other.s_mtime));
    }

    void disp(void) {
        printf("%s_%ld_%lu:%d\n", s_name.c_str(), s_size, s_mtime, fidx);
    }

    void load_from_file(const char* filepath)
    {
        struct stat file_attribue;
        const char * final_slash;

        stat(filepath, &file_attribue);
        final_slash = strrchr(filepath, '/');

        // load filename, size and mtime
        // Save only the basename of the filepath
        this->s_name.assign((final_slash == NULL) ? filepath : final_slash + 1);
        this->s_size = file_attribue.st_size;
        this->s_mtime = file_attribue.st_mtime;

        this->fidx = -1;
    }
};


// --- History -----------------------------------------------------------------
/*! We keep track of transmitted files so that we can retransmit
 * identical slides with the same index, in case the receivers cache
 * them.
 *
 * \c MAXHISTORYLEN defines for how how many slides we want to keep this
 * history.
 */
class History {
    public:
        History() : History(MAXHISTORYLEN) {}
        History(size_t hist_size) :
            m_hist_size(hist_size),
            m_last_given_fidx(0) {}
        void disp_database();
        // controller of id base on database
        int get_fidx(const char* filepath);

    private:
        static const size_t MAXHISTORYLEN;
        static const int    MAXSLIDEID;

        std::deque<fingerprint_t> m_database;

        size_t m_hist_size;

        int m_last_given_fidx;

        // find the fingerprint fp in database.
        // returns the fidx when found,
        //    or   -1 if not found
        int find(const fingerprint_t& fp) const;

        // add a new fingerprint into database
        // returns its fidx
        void add(fingerprint_t& fp);
};


// --- SlideStore -----------------------------------------------------------------
class SlideStore {
private:
    std::list<slide_metadata_t> slides;
    History history;

    static int FilterSlides(const struct dirent* file);
public:
    bool InitFromDir(const std::string& dir);

    bool Empty() {return slides.empty();}
    void Clear() {slides.clear();}
    slide_metadata_t GetSlide();
};


// --- MOTHeader -----------------------------------------------------------------
class MOTHeader {
private:
    size_t header_size;
    uint8_vector_t data;

    void IncrementHeaderSize(size_t size);
    void AddParamHeader(int pli, int param_id) {data.push_back((pli << 6) | (param_id & 0x3F));}

    void AddExtensionFixedSize(int pli, int param_id, const uint8_t* data_field, size_t data_field_len);
    void AddExtensionVarSize(int param_id, const uint8_t* data_field, size_t data_field_len);
public:
    MOTHeader(size_t body_size, int content_type, int content_subtype);

    void AddExtension(int param_id, const uint8_t* data_field, size_t data_field_len);
    const uint8_vector_t GetData() {return data;}
};


// --- SLSEncoder -----------------------------------------------------------------
class SLSEncoder {
private:
    static const size_t MAXSEGLEN;
    static const int    MINQUALITY;
    static const std::string SLS_PARAMS_SUFFIX;

    void warnOnSmallerImage(size_t height, size_t width, const std::string& fname);
#if HAVE_MAGICKWAND
    size_t resizeImage(MagickWand* m_wand, unsigned char** blob, const std::string& fname, bool* jfif_not_png, size_t max_slide_size);
#endif
    bool parse_sls_param_id(const std::string &key, const std::string &value, uint8_t &target);
    bool check_sls_param_len(const std::string &key, size_t len, size_t len_max);
    void process_mot_params_file(MOTHeader& header, const std::string &params_fname);
    uint8_vector_t createMotHeader(size_t blobsize, int fidx, bool jfif_not_png, const std::string &params_fname);
    void createMscDG(MSCDG* msc, unsigned short int dgtype,
            int *cindex, unsigned short int segnum, unsigned short int lastseg,
            unsigned short int tid, const uint8_t* data,
            unsigned short int datalen);
    DATA_GROUP* packMscDG(MSCDG* msc);

    PADPacketizer* pad_packetizer;
    int cindex_header;
    int cindex_body;
public:
    static const size_t MAXSLIDESIZE_SIMPLE;
    static const int APPTYPE_MOT_START;
    static const int APPTYPE_MOT_CONT;
    static const std::string REQUEST_REREAD_FILENAME;

    SLSEncoder(PADPacketizer* pad_packetizer) : pad_packetizer(pad_packetizer), cindex_header(0), cindex_body(0) {}

    bool encodeSlide(const std::string& fname, int fidx, bool raw_slides, size_t max_slide_size, const std::string& dump_name);
    static bool isSlideParamFileFilename(const std::string& filename);
};

#endif /* SLS_H_ */
