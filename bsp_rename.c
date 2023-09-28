// by ficool2

#define _CRT_SECURE_NO_WARNINGS
#define _CRT_NONSTDC_NO_DEPRECATE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Windows.h>

#include "minizip/mz.h"
#include "minizip/mz_strm.h"
#include "minizip/mz_strm_mem.h"
#include "minizip/mz_zip.h"
#include "minizip/mz_zip_rw.h"

#define BSP_ID          (('P' << 24) + ('S' << 16) + ('B' << 8) + 'V')
#define BSP_MAX_LUMPS   64

enum
{
    LUMP_GAME_LUMP              = 35,
    LUMP_PAKFILE                = 40,
    LUMP_TEXDATA_STRING_DATA    = 43,
    LUMP_TEXDATA_STRING_TABLE   = 44,
};

typedef struct
{
    int    offset;
    int    length;
    int    version;
    char   fourCC[4];
} BSPLump;

typedef struct
{
    int         ident;
    int         version;
    BSPLump     lumps[BSP_MAX_LUMPS];
    int         map_revision;
} BSPHeader;

typedef struct
{
    int             id;      
    unsigned short  flags;   
    unsigned short  version; 
    int             offset; 
    int             length; 
} GameLump;

typedef struct
{
    int         count;
    GameLump    lumps[];
} GameLumpHeader;

typedef struct
{
    char*       filename;
    char*       buffer;
    size_t      length;
} ZipFile;

typedef struct
{
    char        find[_MAX_PATH];
    int         find_len;
    char        replace[_MAX_PATH];
    int         replace_len;
    int         is_material;
    char*       material_find;
    char*       material_replace;
} RenamePair;

int round_upper(int num, int multiple)
{
    return ((num + (multiple - 1)) / multiple) * multiple;
}

size_t strlcpy(char* dst, const char* src, size_t len)
{
    size_t src_len = strlen(src);
    if (len) 
    {
        size_t copy_len = src_len >= len ? len : src_len;
        memcpy(dst, src, copy_len);
        dst[copy_len] = '\0';
    }
    return src_len;
}

char* strreplacechar(char* str, char find, char replace)
{
    char* current_pos = strchr(str, find);
    for (char* p = current_pos; (current_pos = strchr(str, find)) != NULL; *current_pos = replace);
    return str;
}

char* strreplacestr(char* orig, char* rep, char* with, size_t* out_len)
{
    int len_with = (int)strlen(with);
    int len_rep = (int)strlen(rep);

    char* tmp;
    char* result;
    char* ins = orig;
    int count;
    for (count = 0; tmp = strstr(ins, rep); ++count) 
        ins = tmp + len_rep;

    size_t result_len = strlen(orig) + (len_with - len_rep) * count;
    tmp = result = malloc(result_len + 1);
    while (count--) 
    {
        ins = strstr(orig, rep);
        int len_front = (int)(ins - orig);
        tmp = strncpy(tmp, orig, len_front) + len_front;
        tmp = strcpy(tmp, with) + len_with;
        orig += len_front + len_rep;
    }

    strcpy(tmp, orig);
    *out_len = result_len;
    return result;
}

void print_progress(size_t processed, size_t total)
{
    printf("Progress: %llu/%llu (%2.0f%%)           \r", processed, total, total > 0 ? ((processed / (float)total) * 100.0) : 0.f);
}

void wait_for_key()
{
    printf("Press any key to continue...\n");
    getchar();
}

RenamePair create_rename_pair(const char* format, const char* find, const char* replace)
{
    RenamePair pair;
    pair.find_len = sprintf(pair.find, format, find);
    pair.replace_len = sprintf(pair.replace, format, replace);
    pair.is_material = !strncmp(format, "materials/", strlen("materials/")) ? 1 : 0;
    pair.material_find = pair.find + strlen("materials/");
    pair.material_replace = pair.replace + strlen("materials/");
    return pair;
}

int main(int argc, char* argv[])
{
    printf("BSP Renamer by ficool2 (%s)\n\n", __DATE__);

    char bsp_name[_MAX_PATH];
    char bsp_out_name[_MAX_PATH];

    if (argc < 3)
    {
        if (argc < 2)
        {
            printf("Enter the bsp name:\n");
            scanf(" %[^\n]", bsp_name);
        }
        else
        {
            printf("Input: %s\n", argv[1]);
        }

        printf("Enter the new bsp name:\n");
        scanf(" %[^\n]", bsp_out_name);
    }
    else
    {
        strcpy(bsp_name, argv[1]);
        strcpy(bsp_out_name, argv[2]);
    }

    if (!strstr(bsp_name, ".bsp"))
        strcat(bsp_name, ".bsp");
    if (!strstr(bsp_out_name, ".bsp"))
        strcat(bsp_out_name, ".bsp");


    /* ============ Read ============ */

    FILE* bsp = fopen(bsp_name, "rb");
    if (!bsp)
    {
        printf("Failed to open %s\n", bsp_name);
        wait_for_key();
        return 1;
    }

    fseek(bsp, 0, SEEK_END);
    size_t bsp_size = ftell(bsp);
    fseek(bsp, 0, SEEK_SET);
    char* bsp_data = malloc(bsp_size);
    fread(bsp_data, 1, bsp_size, bsp);
    fclose(bsp);

    BSPHeader* header = (BSPHeader*)bsp_data;
    if (header->ident != BSP_ID)
    {
        printf("File %s is not a valid BSP!\n", bsp_name);
        free(bsp_data);
        wait_for_key();
        return 1;
    }

    printf("Reading %s...\n", bsp_name);

    BSPLump* lump_pak = &header->lumps[LUMP_PAKFILE];

    char* zip_buf = bsp_data + lump_pak->offset;
    int zip_len = lump_pak->length;
    int zip_compress = -1;

    void* zip_stream_read = mz_zip_reader_create();
    void* zip_stream_read_mem = mz_stream_mem_create();

    mz_stream_mem_set_buffer(zip_stream_read_mem, zip_buf, zip_len);
    mz_zip_reader_open(zip_stream_read, zip_stream_read_mem);

    mz_zip_reader_goto_first_entry(zip_stream_read);
    unsigned int zip_entry_count = 0;
    do
    {
        zip_entry_count++;

        if (zip_compress == -1)
        {
            mz_zip_reader_entry_open(zip_stream_read);

            mz_zip_file* file_info = NULL;
            mz_zip_reader_entry_get_info(zip_stream_read, &file_info);
            zip_compress = file_info->compression_method == MZ_COMPRESS_METHOD_LZMA ? 1 : 0;
            mz_zip_reader_entry_close(zip_stream_read);
        }
    } while (mz_zip_reader_goto_next_entry(zip_stream_read) == MZ_OK);

    printf("Map is%s compressed\n", zip_compress ? "" : " NOT");

    ZipFile** zip_files = malloc(sizeof(ZipFile) * zip_entry_count);
    unsigned int zip_entry_orig_count = zip_entry_count;
    zip_entry_count = 0;

    printf("Reading files...\n");

    mz_zip_reader_goto_first_entry(zip_stream_read);
    do
    {
        mz_zip_reader_entry_open(zip_stream_read);

        mz_zip_file* file_info = NULL;
        mz_zip_reader_entry_get_info(zip_stream_read, &file_info);

        int64_t file_size = file_info->uncompressed_size;
        char* file_buffer = malloc(file_size + 1);
        file_buffer[file_size] = '\0';

        mz_zip_reader_entry_read(zip_stream_read, file_buffer, (int32_t)file_size);
        mz_zip_reader_entry_close(zip_stream_read);

        ZipFile* zip_file = malloc(sizeof(ZipFile));
        zip_files[zip_entry_count++] = zip_file;
        zip_file->filename = strdup(file_info->filename);
        zip_file->buffer = file_buffer;
        zip_file->length = file_size;

        print_progress(zip_entry_count, zip_entry_orig_count);
    } while (mz_zip_reader_goto_next_entry(zip_stream_read) == MZ_OK);

    mz_zip_reader_close(zip_stream_read);

    mz_zip_reader_delete(&zip_stream_read);
    mz_stream_mem_delete(&zip_stream_read_mem);

    printf("Found %u files in pak file           \n", zip_entry_count);

    /* ============ Operate ============ */

    printf("Renaming files...\n");

    const char* bsp_name_path = strrchr(bsp_name, '/');
    if (!bsp_name_path)
        bsp_name_path = strrchr(bsp_name, '\\');
    if (!bsp_name_path)
        bsp_name_path = bsp_name;
    char* bsp_name_ext = strrchr(bsp_name_path, '.');

    char bsp_name_base[_MAX_PATH];
    strlcpy(bsp_name_base, bsp_name_path, bsp_name_ext - bsp_name_path);

    char bsp_out_name_base[_MAX_PATH];
    char* bsp_out_name_ext = strrchr(bsp_out_name, '.');
    strlcpy(bsp_out_name_base, bsp_out_name, bsp_out_name_ext - bsp_out_name);

    RenamePair rename_pairs[] =
    {
        /* must be first */
        create_rename_pair("materials/maps/%s/", bsp_name_base, bsp_out_name_base),

        create_rename_pair("materials/vgui/maps/menu_photos_%s", bsp_name_base, bsp_out_name_base),
        create_rename_pair("maps/%s", bsp_name_base, bsp_out_name_base),
        create_rename_pair("scripts/soundscapes_%s", bsp_name_base, bsp_out_name_base),
    };

    char file_name_buffer[_MAX_PATH];
    for (unsigned int i = 0; i < zip_entry_count; i++)
    {
        ZipFile* zip_file = zip_files[i];

        for (int j = 0; j < ARRAYSIZE(rename_pairs); j++)
        {
            RenamePair* rename_pair = &rename_pairs[j];
            if (!strncmp(zip_file->filename, rename_pair->find, rename_pair->find_len))
            {
                strlcpy(file_name_buffer, rename_pair->replace, rename_pair->replace_len);
                strcat(file_name_buffer, zip_file->filename + rename_pair->find_len);

                printf("Renaming file:\n\t%s\n\t%s\n", zip_file->filename, file_name_buffer);

                free(zip_file->filename);
                zip_file->filename = strdup(file_name_buffer);

                if (rename_pair->is_material)
                {
                    const char* file_ext = strrchr(zip_file->filename, '.');
                    if (file_ext)
                    {
                        ++file_ext;
                        if (!strcmp(file_ext, "vmt"))
                        {
                            printf("Fixing VMT:\n\t%s\n", zip_file->filename);

                            size_t new_length = 0;
                            char* new_buffer;

                            strreplacechar(zip_file->buffer, '\\', '/');
                            new_buffer = strreplacestr(zip_file->buffer, rename_pair->material_find, rename_pair->material_replace, &new_length);

                            free(zip_file->buffer);
                            zip_file->buffer = new_buffer;
                            zip_file->length = new_length;
                        }
                    }
                }
                break;
            }
        }
    }

    printf("Shifting texture table...\n");

    BSPLump* lump_texdata = &header->lumps[LUMP_TEXDATA_STRING_DATA];
    BSPLump* lump_textable = &header->lumps[LUMP_TEXDATA_STRING_TABLE];

    int texdata_end = lump_texdata->offset + lump_texdata->length;
    for (int i = lump_texdata->offset; i < texdata_end; i++)
        if (bsp_data[i] == '\0')
            bsp_data[i] = '\x01';
    bsp_data[texdata_end - 1] = '\0';

    RenamePair* rename_pair = &rename_pairs[0];
    size_t texdata_length = 0;
    char* texdata_buffer = strreplacestr(bsp_data + lump_texdata->offset, rename_pair->material_find, rename_pair->material_replace, &texdata_length);
    texdata_buffer[texdata_length] = '\x01';
    texdata_length += 1;

    int* textable = (int*)&bsp_data[lump_textable->offset];
    int textable_idx = 0;
    int textable_offset = 0;

    for (size_t i = 0; i < texdata_length; i++)
    {
        if (texdata_buffer[i] == '\x01')
        {
            texdata_buffer[i] = '\0';

            textable[textable_idx++] = textable_offset;
            textable_offset = (int)i + 1;
        }
    }

    int texdata_offset = lump_texdata->offset;
    int texdata_diff = (int)texdata_length - lump_texdata->length;
    int align_diff = round_upper(texdata_diff, 4);
    lump_texdata->length += texdata_diff;

    size_t old_bsp_size = bsp_size;
    bsp_size += align_diff;

    if (bsp_size > old_bsp_size)
    {
        bsp_data = realloc(bsp_data, bsp_size);
        header = (BSPHeader*)bsp_data;
        lump_pak = &header->lumps[LUMP_PAKFILE];
        lump_texdata = &header->lumps[LUMP_TEXDATA_STRING_DATA];
    }

    memmove(&bsp_data[texdata_end + align_diff], &bsp_data[texdata_end], old_bsp_size - texdata_end);
    memcpy(&bsp_data[lump_texdata->offset], texdata_buffer, texdata_length);

    for (int i = 0; i < BSP_MAX_LUMPS; i++)
    {
        BSPLump* lump = &header->lumps[i];
        if (lump->offset > texdata_offset)
            lump->offset += align_diff;
    }

    BSPLump* lump_game = &header->lumps[LUMP_GAME_LUMP];
    GameLumpHeader* game_lump_hdr = (GameLumpHeader*)&bsp_data[lump_game->offset];

    for (int i = 0; i < game_lump_hdr->count; i++)
    {
        GameLump* game_lump = &game_lump_hdr->lumps[i];
        if (game_lump->offset > lump_texdata->offset)
            game_lump->offset += align_diff;
    }

    free(texdata_buffer);

    /* ============ Write ============ */

    printf("Writing BSP %s...\n", bsp_out_name);

    FILE* bsp_out = fopen(bsp_out_name, "wb");
    if (bsp_out)
    {
        printf("Writing files...\n");

        void* zip_stream_write = mz_zip_writer_create();
        void* zip_stream_write_mem = mz_stream_mem_create();
        mz_stream_mem_set_grow_size(zip_stream_write_mem, (1024 * 1024 * 128)); /* ~128mb */
        mz_stream_open(zip_stream_write_mem, NULL, MZ_OPEN_MODE_CREATE);
        mz_zip_writer_open(zip_stream_write, zip_stream_write_mem, 0);

        if (zip_compress)
        {
            mz_zip_writer_set_compress_method(zip_stream_write, MZ_COMPRESS_METHOD_LZMA);
            mz_zip_writer_set_compress_level(zip_stream_write, 5);
        }
        else
        {
            mz_zip_writer_set_compress_method(zip_stream_write, MZ_COMPRESS_METHOD_STORE);
            mz_zip_writer_set_compress_level(zip_stream_write, MZ_COMPRESS_LEVEL_DEFAULT);
        }

        time_t the_time = time(NULL);
        for (size_t i = 0; i < zip_entry_count; i++)
        {
            ZipFile* zip_file = zip_files[i];

            mz_zip_file write_file_info = { 0 };
            write_file_info.filename = zip_file->filename;
            write_file_info.modified_date = the_time;
            write_file_info.version_madeby = MZ_VERSION_BUILD;
            write_file_info.compression_method = zip_compress ? MZ_COMPRESS_METHOD_LZMA : MZ_COMPRESS_METHOD_STORE;
            write_file_info.flag = MZ_ZIP_FLAG_UTF8;
            write_file_info.zip64 = MZ_ZIP64_DISABLE;

            mz_zip_writer_entry_open(zip_stream_write, &write_file_info);
            mz_zip_writer_entry_write(zip_stream_write, zip_file->buffer, (int32_t)zip_file->length);
            mz_zip_writer_entry_close(zip_stream_write);

            print_progress(i, zip_entry_count);
        }

        printf("OK                              \n");

        mz_zip_writer_close(zip_stream_write);

        mz_stream_mem_get_buffer(zip_stream_write_mem, (const void**)&zip_buf);
        mz_stream_mem_get_buffer_length(zip_stream_write_mem, &zip_len);

        lump_pak->length = zip_len;
        fwrite(bsp_data, 1, lump_pak->offset, bsp_out);
        fwrite(zip_buf, 1, lump_pak->length, bsp_out);
        fclose(bsp_out);

        mz_zip_writer_delete(&zip_stream_write);
        mz_stream_mem_delete(&zip_stream_write_mem);

        printf("Done!\n");
    }
    else
    {
        printf("\nERROR: Failed to open BSP for writing\n\n");
    }

    free(bsp_data);

    for (size_t i = 0; i < zip_entry_count; i++)
    {
        ZipFile* zip_file = zip_files[i];
        free(zip_file->filename);
        free(zip_files[i]);
    }
    free(zip_files);

    wait_for_key();
    return 0;
}