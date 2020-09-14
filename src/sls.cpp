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
    \file sls.cpp
    \brief Slideshow related code

    \author Sergio Sagliocco <sergio.sagliocco@csp.it>
    \author Matthias P. Braendli <matthias@mpb.li>
    \author Stefan Pöschel <odr@basicmaster.de>
*/

#include "sls.h"


// --- History -----------------------------------------------------------------
const size_t History::MAXHISTORYLEN   =    50; // How many slides to keep in history
const int    History::MAXSLIDEID      =  9999; // Roll-over value for fidx

int History::find(const fingerprint_t& fp) const
{
    // return the id of fingerprint found
    for (const fingerprint_t& db_fp : m_database)
        if (db_fp == fp)
            return db_fp.fidx;

    // return -1 when the database doesn't contain this fingerprint
    return -1;
}


void History::add(fingerprint_t& fp)
{
    m_database.push_back(fp);

    if (m_database.size() > m_hist_size) {
        m_database.pop_front();
    }
}


void History::disp_database()
{
    size_t id;
    printf("HISTORY DATABASE:\n");
    if (m_database.empty()) {
        printf(" empty\n");
    }
    else {
        for (id = 0; id < m_database.size(); id++) {
            printf(" id %4zu: ", id);
            m_database[id].disp();
        }
    }
    printf("-----------------\n");
}


int History::get_fidx(const char* filepath)
{
    fingerprint_t fp;

    fp.load_from_file(filepath);

    int idx = find(fp);

    if (idx < 0) {
        idx = m_last_given_fidx++;
        fp.fidx = idx;

        if (m_last_given_fidx > MAXSLIDEID) {
            m_last_given_fidx = 0;
        }

        add(fp);
    }

    return idx;
}


// --- SlideStore -----------------------------------------------------------------
int SlideStore::FilterSlides(const struct dirent* file) {
    std::string name = file->d_name;

    // skip '.'/'..' dirs
    if(name == "." || name == "..")
        return 0;

    // skip slide params files
    if(SLSEncoder::isSlideParamFileFilename(name))
        return 0;

    // skip re-read request file
    if(name == SLSEncoder::REQUEST_REREAD_FILENAME.c_str())
        return 0;

    return 1;
}

bool SlideStore::InitFromDir(const std::string& dir) {
    // start with empty list
    Clear();

    struct dirent** dir_entries;
    int dir_count = scandir(dir.c_str(), &dir_entries, FilterSlides, alphasort);
    if (dir_count < 0) {
        perror(("ODR-PadEnc Error: cannot open slides directory '" + dir + "'").c_str());
        return false;
    }

    // add new slides to transmit to list
    for (int i = 0; i < dir_count; i++) {
        std::string imagepath = dir + "/" + std::string(dir_entries[i]->d_name);
        free(dir_entries[i]);

        slide_metadata_t md;
        md.filepath = imagepath;
        md.fidx     = history.get_fidx(imagepath.c_str());
        slides.push_back(md);

        if (verbose)
            fprintf(stderr, "ODR-PadEnc found slide '%s', fidx %d\n", imagepath.c_str(), md.fidx);
    }

    free(dir_entries);

#ifdef DEBUG
    history.disp_database();
#endif

    // sort the list in fidx order
    slides.sort();

    return true;
}

slide_metadata_t SlideStore::GetSlide() {
    // pre-condition: list non-empty

    slide_metadata_t slide = slides.front();
    slides.pop_front();
    return slide;
}


// --- MOTHeader -----------------------------------------------------------------
MOTHeader::MOTHeader(size_t body_size, int content_type, int content_subtype)
: header_size(0), data(uint8_vector_t(7, 0x00)) {
    // init header core

    // body size
    data[0] = (body_size >> 20) & 0xFF;
    data[1] = (body_size >> 12) & 0xFF;
    data[2] = (body_size >>  4) & 0xFF;
    data[3] = (body_size <<  4) & 0xF0;

    // header size
    IncrementHeaderSize(data.size());

    // content type
    data[5] |= (content_type << 1) & 0x7E;

    // content subtype
    data[5] |= (content_subtype >> 8) & 0x01;
    data[6] |=  content_subtype       & 0xFF;
}


void MOTHeader::IncrementHeaderSize(size_t size) {
    header_size += size;

    data[3] &= 0xF0;
    data[3] |= (header_size >> 9) & 0x0F;

    data[4]  = (header_size >> 1) & 0xFF;

    data[5] &= 0x7F;
    data[5] |= (header_size << 7) & 0x80;
}


void MOTHeader::AddExtensionFixedSize(int pli, int param_id, const uint8_t* data_field, size_t data_field_len) {
    AddParamHeader(pli, param_id);

    for (size_t i = 0; i < data_field_len; i++)
        data.push_back(data_field[i]);

    IncrementHeaderSize(1 + data_field_len);
}


void MOTHeader::AddExtensionVarSize(int param_id, const uint8_t* data_field, size_t data_field_len) {
    AddParamHeader(0x3, param_id);

    // longer field lens use 15 instead of 7 bits
    bool ext = data_field_len > 127;
    if (ext) {
        data.push_back(0x80 | ((data_field_len >> 8) & 0x7F));
        data.push_back(data_field_len & 0xFF);
    } else {
        data.push_back(data_field_len & 0x7F);
    }

    for (size_t i = 0; i < data_field_len; i++)
        data.push_back(data_field[i]);

    IncrementHeaderSize(1 + (ext ? 2 : 1) + data_field_len);
}


void MOTHeader::AddExtension(int param_id, const uint8_t* data_field, size_t data_field_len) {
    int pli;

    switch(data_field_len) {
    case 0:
        pli = 0x0;  // 0b00
        break;
    case 1:
        pli = 0x1;  // 0b01
        break;
    case 4:
        pli = 0x2;  // 0b10
        break;
    default:
        pli = 0x3;  // 0b11
        break;
    }

    if (pli == 0x3)
        AddExtensionVarSize(param_id, data_field, data_field_len);
    else
        AddExtensionFixedSize(pli, param_id, data_field, data_field_len);
}


// --- SLSEncoder -----------------------------------------------------------------
const size_t SLSEncoder::MAXSEGLEN              =  1013; // Bytes (EN 301 234 v2.1.1, ch. 5.1.1 limits to 8189); the complete DG will be 1024 bytes
const size_t SLSEncoder::MAXSLIDESIZE_SIMPLE    = 51200; // Bytes (TS 101 499 v3.1.1, ch. 9.1.2)
const int    SLSEncoder::MINQUALITY             =    40; // Do not allow the image compressor to go below JPEG quality 40
const std::string SLSEncoder::SLS_PARAMS_SUFFIX = ".sls_params";
const int SLSEncoder::APPTYPE_MOT_START = 12;
const int SLSEncoder::APPTYPE_MOT_CONT = 13;
const std::string SLSEncoder::REQUEST_REREAD_FILENAME = "REQUEST_SLIDES_DIR_REREAD";


void SLSEncoder::warnOnSmallerImage(size_t height, size_t width, const std::string& fname) {
    if (height < 240 || width < 320)
        fprintf(stderr, "ODR-PadEnc Warning: Image '%s' smaller than recommended size (%zu x %zu < 320 x 240 px)\n", fname.c_str(), width, height);
}


/*! Scales the image down if needed,
 * so that it is 320x240 pixels.
 * Automatically reduces the quality to make sure the
 * blobsize is not too large.
 *
 * \return the blobsize
 */
#if HAVE_MAGICKWAND
size_t SLSEncoder::resizeImage(MagickWand* m_wand, unsigned char** blob, const std::string& fname, bool* jfif_not_png, size_t max_slide_size)
{
    unsigned char* blob_png;
    unsigned char* blob_jpg;
    size_t blobsize_png;
    size_t blobsize_jpg;

    size_t height = MagickGetImageHeight(m_wand);
    size_t width  = MagickGetImageWidth(m_wand);

    while (height > 240 || width > 320) {
        if (height/240.0 > width/320.0) {
            width = width * 240.0 / height;
            height = 240;
        }
        else {
            height = height * 320.0 / width;
            width = 320;
        }
#ifdef HAVE_MAGICKWAND_LEGACY
        MagickResizeImage(m_wand, width, height, LanczosFilter, 1);
#else
        MagickResizeImage(m_wand, width, height, LanczosFilter);
#endif
    }

    height = MagickGetImageHeight(m_wand);
    width  = MagickGetImageWidth(m_wand);

    // try PNG (zlib level 9 / possibly adaptive filtering)
    MagickSetImageFormat(m_wand, "png");
    MagickSetImageCompressionQuality(m_wand, 95);
    blob_png = MagickGetImageBlob(m_wand, &blobsize_png);

    // try JPG
    MagickSetImageFormat(m_wand, "jpg");

    blob_jpg = NULL;
    int quality_jpg = 100;
    do {
        MagickRelinquishMemory(blob_jpg);
        quality_jpg -= 5;

        MagickSetImageCompressionQuality(m_wand, quality_jpg);
        blob_jpg = MagickGetImageBlob(m_wand, &blobsize_jpg);
    } while (blobsize_jpg > max_slide_size && quality_jpg > MINQUALITY);


    // check for max size
    if (blobsize_png > max_slide_size && blobsize_jpg > max_slide_size) {
        fprintf(stderr, "ODR-PadEnc: Image Size too large after compression: %zu bytes (PNG), %zu bytes (JPEG)\n",
                blobsize_png, blobsize_jpg);
        MagickRelinquishMemory(blob_png);
        MagickRelinquishMemory(blob_jpg);
        return 0;
    }

    // choose the smaller one (at least one does not exceed the max size)
    *jfif_not_png = blobsize_jpg < blobsize_png;

    if (verbose) {
        if (*jfif_not_png)
            fprintf(stderr, "ODR-PadEnc resized image to %zu x %zu. Size after compression %zu bytes (JPEG, q=%d; PNG was %zu bytes)\n",
                    width, height, blobsize_jpg, quality_jpg, blobsize_png);
        else
            fprintf(stderr, "ODR-PadEnc resized image to %zu x %zu. Size after compression %zu bytes (PNG; JPEG was %zu bytes)\n",
                    width, height, blobsize_png, blobsize_jpg);
    }

    // warn if resized image smaller than default dimension
    warnOnSmallerImage(height, width, fname);

    MagickRelinquishMemory(*jfif_not_png ? blob_png : blob_jpg);
    *blob = *jfif_not_png ? blob_jpg : blob_png;
    return *jfif_not_png ? blobsize_jpg : blobsize_png;
}
#endif

static void dump_slide(const std::string& dump_name, const uint8_t *blob, size_t size)
{
    FILE* fd = fopen(dump_name.c_str(), "w");

    if (fd == nullptr) {
        perror(("ODR-PadEnc Error: Unable to open file '" + dump_name + "' for writing").c_str());
        return;
    }

    if (fwrite(blob, size, 1, fd) == 0) {
        perror(("ODR-PadEnc Error: Unable to write to file '" + dump_name + "'").c_str());
    }

    fclose(fd);
}

static bool filename_specifies_raw_mode(const std::string& fname)
{
    const size_t sep = fname.rfind("_");
    if (sep != std::string::npos) {
        std::string suffix = fname.substr(sep, std::string::npos);
        std::transform(suffix.begin(), suffix.end(), suffix.begin(), ::tolower);
        return suffix == "_padencrawmode.png" or suffix == "_padencrawmode.jpg";
    }
    else {
        return false;
    }
}

bool SLSEncoder::encodeSlide(const std::string& fname, int fidx, bool raw_slides, size_t max_slide_size, const std::string& dump_name)
{
    bool result = false;

#if HAVE_MAGICKWAND
    MagickWand *m_wand = NULL;
#endif

    uint8_t *raw_blob = NULL;
    uint8_t *magick_blob = NULL;
    size_t blobsize;
    bool jfif_not_png = true;

    const bool raw_slide = filename_specifies_raw_mode(fname) or raw_slides;

    if (!raw_slide) {
#if HAVE_MAGICKWAND
        /*! By default, we do resize the image to 320x240, with a quality such that
         * the blobsize is at most MAXSLIDESIZE.
         *
         * For JPEG input files that are already at the right resolution and at the
         * right blobsize, we disable this to avoid quality loss due to recompression
         *
         * As device support of this feature is optional, we furthermore require JPEG input
         * files to not have progressive coding.
         */
        bool native_support = false;
        bool resize_required = true;

        m_wand = NewMagickWand();

        if (MagickReadImage(m_wand, fname.c_str()) == MagickFalse) {
            fprintf(stderr, "ODR-PadEnc Error: Unable to load image '%s'\n",
                    fname.c_str());

            goto encodefile_out;
        }

        size_t height       = MagickGetImageHeight(m_wand);
        size_t width        = MagickGetImageWidth(m_wand);
        char*  orig_format  = MagickGetImageFormat(m_wand);
        bool   jpeg_progr   = MagickGetImageInterlaceScheme(m_wand) == JPEGInterlace;

        // strip unneeded information (profiles, meta data)
        MagickStripImage(m_wand);

        if (orig_format) {
            if (strcmp(orig_format, "JPEG") == 0) {
                size_t orig_quality = MagickGetImageCompressionQuality(m_wand);
                native_support = true;

                if (verbose) {
                    fprintf(stderr, "ODR-PadEnc image: '" ODR_COLOR_SLS "%s" ODR_COLOR_RST "' (id=%d)."
                            " Original size: %zu x %zu. (%s, q=%zu, progr=%s)\n",
                            fname.c_str(), fidx, width, height, orig_format, orig_quality, jpeg_progr ? "y" : "n");
                }
            }
            else if (strcmp(orig_format, "PNG") == 0) {
                native_support = true;
                jfif_not_png = false;

                if (verbose) {
                    fprintf(stderr, "ODR-PadEnc image: '" ODR_COLOR_SLS "%s" ODR_COLOR_RST "' (id=%d)."
                            " Original size: %zu x %zu. (%s)\n",
                            fname.c_str(), fidx, width, height, orig_format);
                }
            }
            else if (verbose) {
                fprintf(stderr, "ODR-PadEnc image: '" ODR_COLOR_SLS "%s" ODR_COLOR_RST "' (id=%d)."
                        " Original size: %zu x %zu. (%s)\n",
                        fname.c_str(), fidx, width, height, orig_format);
            }

            free(orig_format);
        }
        else {
            fprintf(stderr, "ODR-PadEnc Warning: Unable to detect image format of '%s'\n",
                    fname.c_str());

            fprintf(stderr, "ODR-PadEnc image: '" ODR_COLOR_SLS "%s" ODR_COLOR_RST "' (id=%d).  Original size: %zu x %zu.\n",
                    fname.c_str(), fidx, width, height);
        }

        if (native_support && height <= 240 && width <= 320 && not jpeg_progr) {
            // Don't recompress the image and check if the blobsize is suitable
            magick_blob = MagickGetImageBlob(m_wand, &blobsize);

            if (blobsize <= max_slide_size) {
                if (verbose) {
                    fprintf(stderr, "ODR-PadEnc image: '" ODR_COLOR_SLS "%s" ODR_COLOR_RST "' (id=%d).  No resize needed: %zu Bytes\n",
                            fname.c_str(), fidx, blobsize);
                }
                resize_required = false;
            } else {
                MagickRelinquishMemory(magick_blob);
                magick_blob = NULL;
            }
        }

        if (resize_required) {
            blobsize = resizeImage(m_wand, &magick_blob, fname, &jfif_not_png, max_slide_size);
        } else {
            // warn if unresized image smaller than default dimension
            warnOnSmallerImage(height, width, fname);
        }

#else
        fprintf(stderr, "ODR-PadEnc has not been compiled with MagickWand, only RAW slides are supported!\n");
        goto encodefile_out;
#endif
    }
    else { // Use RAW data, it might not even be a jpg !
        // read file
        FILE* pFile = fopen(fname.c_str(), "rb");
        if (pFile == NULL) {
            perror(("ODR-PadEnc Error: Unable to load file '" + fname + "'").c_str());
            goto encodefile_out;
        }

        // obtain file size:
        fseek(pFile, 0, SEEK_END);
        blobsize = ftell(pFile);
        rewind(pFile);

        if (verbose) {
            fprintf(stderr, "ODR-PadEnc image: '" ODR_COLOR_SLS "%s" ODR_COLOR_RST "' (id=%d). Raw file: %zu Bytes\n",
                    fname.c_str(), fidx, blobsize);
        }

        if (blobsize > max_slide_size) {
            fprintf(stderr, "ODR-PadEnc Warning: blob in raw-slide '%s' too large\n",
                    fname.c_str());
        }

        // allocate memory to contain the whole file:
        raw_blob = (uint8_t*) malloc(blobsize);
        if (raw_blob == NULL) {
            fprintf(stderr, "ODR-PadEnc Error: Memory allocation error\n");
            fclose(pFile);
            goto encodefile_out;
        }

        // copy the file into the buffer:
        if (fread(raw_blob, blobsize, 1, pFile) != 1) {
            fprintf(stderr, "ODR-PadEnc Error: Could not read file\n");
            fclose(pFile);
            goto encodefile_out;
        }

        size_t last_dot = fname.rfind(".");

        // default:
        jfif_not_png = true; // This is how we did it in the past.
                             // It's wrong anyway, so we're at least compatible

        if (last_dot != std::string::npos) {
            std::string file_extension = fname.substr(last_dot, std::string::npos);

            std::transform(file_extension.begin(), file_extension.end(), file_extension.begin(), ::tolower);

            if (file_extension == ".png") {
                jfif_not_png = false;
            }
        }

        if (pFile != NULL) {
            fclose(pFile);
        }
    }

    if (blobsize) {
        if (raw_blob == nullptr and magick_blob == nullptr) {
            fprintf(stderr, "ODR-PadEnc logic error: either raw_blob or magick_blob must be non-null! See src/sls.cpp line %d\n", __LINE__);
            abort();
        }
        const uint8_t *blob = raw_blob ? raw_blob : magick_blob;

        MSCDG msc;
        DATA_GROUP* dgli;
        DATA_GROUP* mscdg;

        size_t nseg = blobsize / MAXSEGLEN;
        size_t lastseglen = blobsize % MAXSEGLEN;
        if (lastseglen)
            nseg++;

        // MOT Header

        uint8_vector_t mothdr = createMotHeader(blobsize, fidx, jfif_not_png, fname + SLS_PARAMS_SUFFIX);
        // Create the MSC Data Group C-Structure
        createMscDG(&msc, 3, &cindex_header, 0, 1, fidx, &mothdr[0], mothdr.size());
        // Generate the MSC DG frame (Figure 9 en 300 401)
        mscdg = packMscDG(&msc);
        dgli = PADPacketizer::CreateDataGroupLengthIndicator(mscdg->data.size());

        pad_packetizer->AddDG(dgli, false);
        pad_packetizer->AddDG(mscdg, false);

        // MOT Body

        for (size_t i = 0; i < nseg; i++) {
            const uint8_t *curseg = blob + i * MAXSEGLEN;
            size_t curseglen;
            int last;

            if (i == nseg - 1) {
                curseglen = lastseglen;
                last = 1;
            } else {
                curseglen = MAXSEGLEN;
                last = 0;
            }

            createMscDG(&msc, 4, &cindex_body, i, last, fidx, curseg, curseglen);
            mscdg = packMscDG(&msc);
            dgli = PADPacketizer::CreateDataGroupLengthIndicator(mscdg->data.size());

            pad_packetizer->AddDG(dgli, false);
            pad_packetizer->AddDG(mscdg, false);
        }

        if (not dump_name.empty()) {
            dump_slide(dump_name, blob, blobsize);
        }

        result = true;
    }

encodefile_out:
    if (raw_blob) {
        free(raw_blob);
    }

    if (magick_blob) {
#if HAVE_MAGICKWAND
        MagickRelinquishMemory(magick_blob);
#endif
    }

#if HAVE_MAGICKWAND
    if (m_wand) {
        m_wand = DestroyMagickWand(m_wand);
    }
#endif

    return result;
}


bool SLSEncoder::parse_sls_param_id(const std::string &key, const std::string &value, uint8_t &target) {
    int value_int = atoi(value.c_str());
    if (value_int >= 0x00 && value_int <= 0xFF) {
        target = value_int;
        return true;
    }
    fprintf(stderr, "ODR-PadEnc Warning: SLS parameter '%s' %d out of range - ignored\n", key.c_str(), value_int);
    return false;
}


bool SLSEncoder::check_sls_param_len(const std::string &key, size_t len, size_t len_max) {
    if (len <= len_max)
        return true;
    fprintf(stderr, "ODR-PadEnc Warning: SLS parameter '%s' exceeds its maximum length (%zu > %zu) - ignored\n", key.c_str(), len, len_max);
    return false;
}


void SLSEncoder::process_mot_params_file(MOTHeader& header, const std::string &params_fname) {
    std::ifstream params_fstream(params_fname);
    if (!params_fstream.is_open())
        return;

    std::string line;
    while (std::getline(params_fstream, line)) {
        // ignore empty lines and comments
        if (line.empty() || line[0] == '#')
            continue;

        // parse key/value pair
        size_t separator_pos = line.find('=');
        if (separator_pos == std::string::npos) {
            fprintf(stderr, "ODR-PadEnc Warning: SLS parameter line '%s' without separator - ignored\n", line.c_str());
            continue;
        }
        std::string key = line.substr(0, separator_pos);
        std::string value = line.substr(separator_pos + 1);
#ifdef DEBUG
        fprintf(stderr, "process_mot_params_file: key: '%s', value: '%s'\n", key.c_str(), value.c_str());
#endif

        if (key == "CategoryID/SlideID") {
            // split value
            std::vector<std::string> params = split_string(value, ' ');
            if (params.size() != 2) {
                fprintf(stderr, "ODR-PadEnc Warning: SLS parameter CategoryID/SlideID value '%s' does not have two parts - ignored\n", value.c_str());
                continue;
            }

            uint8_t id_param[2];
            if (parse_sls_param_id("CategoryID", params[0], id_param[0]) &
                parse_sls_param_id("SlideID", params[1], id_param[1])) {
                header.AddExtension(0x25, id_param, sizeof(id_param));
                if (verbose)
                    fprintf(stderr, "ODR-PadEnc SLS parameter: CategoryID = %d / SlideID = %d\n", id_param[0], id_param[1]);
            }
            continue;
        }
        if (key == "CategoryTitle") {
            if(!check_sls_param_len("CategoryTitle", value.length(), 128))
                continue;

            header.AddExtension(0x26, (uint8_t*) value.c_str(), value.length());
            if (verbose)
                fprintf(stderr, "ODR-PadEnc SLS parameter: CategoryTitle = '%s'\n", value.c_str());
            continue;
        }
        if (key == "ClickThroughURL") {
            if(!check_sls_param_len("ClickThroughURL", value.length(), 512))
                continue;

            header.AddExtension(0x27, (uint8_t*) value.c_str(), value.length());
            if (verbose)
                fprintf(stderr, "ODR-PadEnc SLS parameter: ClickThroughURL = '%s'\n", value.c_str());
            continue;
        }
        if (key == "AlternativeLocationURL") {
            if(!check_sls_param_len("AlternativeLocationURL", value.length(), 512))
                continue;

            header.AddExtension(0x28, (uint8_t*) value.c_str(), value.length());
            if (verbose)
                fprintf(stderr, "ODR-PadEnc SLS parameter: AlternativeLocationURL = '%s'\n", value.c_str());
            continue;
        }

        fprintf(stderr, "ODR-PadEnc Warning: SLS parameter '%s' unknown - ignored\n", key.c_str());
    }
}


uint8_vector_t SLSEncoder::createMotHeader(size_t blobsize, int fidx, bool jfif_not_png, const std::string &params_fname)
{
    // prepare ContentName
    uint8_t cntemp[10];     // = 1 + 8 + 1 = charset + name + terminator
    cntemp[0] = (uint8_t) DABCharset::COMPLETE_EBU_LATIN << 4;
    snprintf((char*) (cntemp + 1), sizeof(cntemp) - 1, "%04d.%s", fidx, jfif_not_png ? "jpg" : "png");

    // MOT header - content type: image, content subtype: JFIF / PNG
    MOTHeader header(blobsize, 0x02, jfif_not_png ? 0x001 : 0x003);

    // TriggerTime: NOW
    uint8_t triggertime_now[4] = {0x00};
    header.AddExtension(0x05, triggertime_now, sizeof(triggertime_now));

    // ContentName: XXXX.jpg / XXXX.png
    header.AddExtension(0x0C, cntemp, sizeof(cntemp) - 1);   // omit terminator

    // process params file if present
    process_mot_params_file(header, params_fname);

    if (verbose)
        fprintf(stderr, "ODR-PadEnc writing image as '%s'\n", cntemp + 1);

    return header.GetData();
}


void SLSEncoder::createMscDG(MSCDG* msc, unsigned short int dgtype,
        int *cindex, unsigned short int segnum, unsigned short int lastseg,
        unsigned short int tid, const uint8_t* data,
        unsigned short int datalen)
{
    msc->extflag = 0;
    msc->crcflag = 1;
    msc->segflag = 1;
    msc->accflag = 1;
    msc->dgtype = dgtype;
    msc->cindex = *cindex;
    msc->rindex = 0;
    msc->last = lastseg;
    msc->segnum = segnum;
    msc->rfa = 0;
    msc->tidflag = 1;
    msc->lenid = 2;
    msc->tid = tid;
    msc->segdata = data;
    msc->rcount = 0;
    msc->seglen = datalen;

    *cindex = (*cindex + 1) % 16;   // increment continuity index
}


DATA_GROUP* SLSEncoder::packMscDG(MSCDG* msc)
{
    DATA_GROUP* dg = new DATA_GROUP(9 + msc->seglen, APPTYPE_MOT_START, APPTYPE_MOT_CONT);
    uint8_vector_t &b = dg->data;

    // headers
    b[0] = (msc->extflag<<7) | (msc->crcflag<<6) | (msc->segflag<<5) |
           (msc->accflag<<4) | msc->dgtype;

    b[1] = (msc->cindex<<4) | msc->rindex;
    b[2] = (msc->last<<7) | ((msc->segnum & 0x7F00) >> 8);
    b[3] =  msc->segnum & 0x00FF;
    b[4] = 0;
    b[4] = (msc->rfa << 5) | (msc->tidflag << 4) | msc->lenid;
    b[5] = (msc->tid & 0xFF00) >> 8;
    b[6] =  msc->tid & 0x00FF;
    b[7] = (msc->rcount << 5) | ((msc->seglen & 0x1F00)>>8);
    b[8] =  msc->seglen & 0x00FF;

    // data field
    memcpy(&b[9], msc->segdata, msc->seglen);

    // CRC
    dg->AppendCRC();

    return dg;
}

bool SLSEncoder::isSlideParamFileFilename(const std::string& filename) {
    return filename.length() >= SLS_PARAMS_SUFFIX.length() &&
           filename.substr(filename.length() - SLS_PARAMS_SUFFIX.length()) == SLS_PARAMS_SUFFIX;
}
