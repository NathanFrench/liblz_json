#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <stdbool.h>
#include <assert.h>
#include <stdarg.h>
#include <ctype.h>
#include <unistd.h>

#include <liblz/lzapi.h>

#include <liblz/lz_tailq.h>
#include <liblz/lz_kvmap.h>
#include <liblz/lz_heap.h>

#include "lz_json.h"

enum lz_j_state {
    lz_j_s_start = 0,
    lz_j_s_end
};

enum lz_j_arr_state {
    lz_j_arr_s_val = 0,
    lz_j_arr_s_comma,
    lz_j_arr_s_end
};

enum lz_j_obj_state {
    lz_j_obj_s_key = 0,
    lz_j_obj_s_delim,
    lz_j_obj_s_val,
    lz_j_obj_s_comma,
    lz_j_obj_s_end
};

typedef enum lz_j_state     lz_j_state;
typedef enum lz_j_arr_state lz_j_arr_state;
typedef enum lz_j_obj_state lz_j_obj_state;

static __thread void * __js_heap = NULL;

struct lz_json_s {
    lz_json_vtype type;
    union {
        lz_kvmap   * object;
        lz_tailq   * array;
        char       * string;
        unsigned int number;
        bool         boolean;
    };

    size_t slen;
    void   (* freefn)(void *);
};

#define _lz_j_type(j) (lz_likely(j) ? j->type : -1)

inline lz_json_vtype
lz_json_get_type(lz_json * js) {
    if (lz_unlikely(js == NULL)) {
        return -1;
    } else {
        return js->type;
    }
}

inline ssize_t
lz_json_get_size(lz_json * js) {
    if (lz_unlikely(js == NULL)) {
        return -1;
    }

    if (js == NULL) {
        return -1;
    }

    switch (_lz_j_type(js)) {
        case lz_json_vtype_string:
            return (ssize_t)js->slen;
        case lz_json_vtype_array:
            return lz_tailq_size(js->array);
        case lz_json_vtype_object:
            return lz_kvmap_get_size(js->object);
        default:
            return 0;
    }

    return 0;
}

inline lz_kvmap *
lz_json_get_object(lz_json * js) {
    if (lz_unlikely(js == NULL)) {
        return NULL;
    }

    if (lz_unlikely(_lz_j_type(js) != lz_json_vtype_object)) {
        return NULL;
    } else {
        return js->object;
    }
}

inline lz_tailq *
lz_json_get_array(lz_json * js) {
    if (js == NULL) {
        return NULL;
    }

    if (lz_unlikely(_lz_j_type(js) != lz_json_vtype_array)) {
        return NULL;
    }

    return js->array;
}

inline unsigned int
lz_json_get_number(lz_json * js) {
    if (js == NULL) {
        return 0;
    }

    if (lz_unlikely(_lz_j_type(js) != lz_json_vtype_number)) {
        return 0;
    } else {
        return js->number;
    }
}

inline const char *
lz_json_get_string(lz_json * js) {
    if (lz_unlikely(js == NULL)) {
        return NULL;
    }

    if (lz_likely(_lz_j_type(js) == lz_json_vtype_string)) {
        return js->string;
    } else {
        return NULL;
    }
}

inline bool
lz_json_get_boolean(lz_json * js) {
    if (js == NULL) {
        return false;
    }

    if (lz_unlikely(_lz_j_type(js) != lz_json_vtype_bool)) {
        return false;
    } else {
        return js->boolean;
    }
}

inline char
lz_json_get_null(lz_json * js) {
    if (js == NULL) {
        return -1;
    }

    if (lz_unlikely(_lz_j_type(js) != lz_json_vtype_null)) {
        return -1;
    } else {
        return 1;
    }
}

static inline lz_json *
_lz_j_new(lz_json_vtype type) {
    lz_json * lz_j;

    if (lz_unlikely(__js_heap == NULL)) {
        __js_heap = lz_heap_new(sizeof(lz_json), 1024);
    }

    lz_j         = lz_heap_alloc(__js_heap);
    lz_alloc_assert(lz_j);

    lz_j->type   = type;
    lz_j->freefn = NULL;
    return lz_j;
}

static inline lz_json *
_lz_j_object_new(void) {
    lz_json * js;

    if (!(js = _lz_j_new(lz_json_vtype_object))) {
        return NULL;
    }

    js->object = lz_kvmap_new(10);
    js->freefn = (void (*))lz_kvmap_free;

    return js;
}

static inline lz_json *
_lz_j_array_new(void) {
    lz_json * js;

    if (!(js = _lz_j_new(lz_json_vtype_array))) {
        return NULL;
    }

    js->array  = lz_tailq_new();
    js->freefn = (void (*))lz_tailq_free;

    return js;
}

static inline lz_json *
_lz_j_string_new(const char * str, size_t slen) {
    lz_json * js;

    js = _lz_j_new(lz_json_vtype_string);
    lz_alloc_assert(js);

    if (lz_unlikely(str == NULL || slen == 0)) {
        slen = 0;
    }

    js->string       = malloc(slen + 1);
    lz_alloc_assert(js->string);

    js->string[slen] = '\0';

    memcpy(js->string, str, slen);

    js->slen         = slen;
    js->freefn       = free;

    return js;
}

static inline lz_json *
_lz_j_number_new(unsigned int num) {
    lz_json * js;

    js         = _lz_j_new(lz_json_vtype_number);
    js->number = num;

    return js;
}

static inline lz_json *
_lz_j_boolean_new(bool boolean) {
    lz_json * js;

    js          = _lz_j_new(lz_json_vtype_bool);
    js->boolean = boolean;

    return js;
}

static inline lz_json *
_lz_j_null_new(void) {
    return _lz_j_new(lz_json_vtype_null);
}

static inline int
_lz_j_object_add(lz_json * dst, const char * key, lz_json * val) {
    if (lz_unlikely(_lz_j_type(dst) != lz_json_vtype_object)) {
        return -1;
    }

    if (!lz_kvmap_add(dst->object, key, val, (void (*))lz_json_free)) {
        return -1;
    }

    return 0;
}

static inline int
_lz_j_object_add_klen(lz_json * dst, const char * key, size_t klen, lz_json * val) {
    if (lz_unlikely(_lz_j_type(dst) != lz_json_vtype_object)) {
        return -1;
    }

    if (!lz_kvmap_add_wklen(dst->object,
                            key, klen, val, (void (*))lz_json_free)) {
        return -1;
    }

    return 0;
}

static inline int
_lz_j_array_add(lz_json * dst, lz_json * src) {
    if (lz_unlikely(_lz_j_type(dst) != lz_json_vtype_array)) {
        return -1;
    }

    if (!lz_tailq_append(dst->array, src, 1, (void (*))lz_json_free)) {
        return -1;
    }

    return 0;
}

lz_json *
lz_json_parse_string(const char * data, size_t len, size_t * n_read) {
    unsigned char ch;
    size_t        i;
    size_t        buflen;
    char          buf[len + 128];
    int           buf_idx;
    int           escaped;
    bool          error;
    lz_json     * js;

    if (!data || !len || *data != '"') {
        /* *n_read = 0; */
        return NULL;
    }

    escaped = 0;
    buf_idx = 0;
    error   = false;
    js      = NULL;
    buflen  = len + 128;

    len--;
    data++;

    for (i = 0; i < len; i++) {
        if (buf_idx >= buflen) {
            error = true;
            errno = ENOBUFS;
            goto end;
        }

        ch = data[i];

        if (!lz_isascii(ch)) {
            error = true;
            goto end;
        }

        if (escaped) {
            switch (ch) {
                case '"':
                case '/':
                case 'b':
                case 'f':
                case 'n':
                case 'r':
                case 't':
                case '\\':
                    escaped        = 0;
                    buf[buf_idx++] = ch;
                    break;
                default:
                    error          = true;
                    goto end;
            }
            continue;
        }

        if (ch == '\\') {
            escaped = 1;
            continue;
        }

        if (ch == '"') {
            js = _lz_j_string_new(buf, buf_idx);
            i += 1;
            break;
        }

        buf[buf_idx++] = ch;
    }

end:
    *n_read += i;

    if (error == true) {
        lz_safe_free(js, lz_json_free);
        return NULL;
    }

    return js;
} /* lz_json_parse_string */

inline lz_json *
lz_json_parse_key(const char * data, size_t len, size_t * n_read) {
    return lz_json_parse_string(data, len, n_read);
}

lz_json *
lz_json_parse_number(const char * data, size_t len, size_t * n_read) {
    unsigned char ch;
    char          buf[len];
    int           buf_idx;
    size_t        i;
    lz_json     * js;

    if (!data || !len) {
        return NULL;
    }

    js      = NULL;
    buf_idx = 0;

    memset(buf, 0, sizeof(buf));

    for (i = 0; i < len; i++) {
        ch = data[i];

        if (!isdigit(ch) || (len == 1 && isdigit(ch))) {
            js = _lz_j_number_new((unsigned int)lz_atoi(buf, buf_idx));
            break;
        }

        buf[buf_idx++] = ch;
    }

    *n_read += (len == 1) ? 1 : i - 1;

    return js;
}

#define J_TRUE_CMP   0x657572
#define J_FALSE_CMP  0x65736c61
#define J_TRUE_MASK  0xFFFFFF
#define J_FALSE_MASK 0xFFFFFFFF

inline lz_json *
lz_json_parse_boolean(const char * data, size_t len, size_t * n_read) {
    lz_json * js;

    if (lz_unlikely(len < 4)) {
        /* need at LEAST 'true' */
        return NULL;
    }

    js = NULL;

    /* here we cast our data string to an integer, mask it by the
     * number of words we want to see, then match the integer version
     * of the string.
     */
    switch (*data) {
        case 't':
            if ((*((uint32_t*)(data + 1)) & J_TRUE_MASK) == J_TRUE_CMP) {
                *n_read += 3;
                js       = _lz_j_boolean_new(true);
            }

            break;
        case 'f':
            if (len < 5) {
                return NULL;
            }

            if ((*((uint32_t*)(data + 1)) & J_FALSE_MASK) == J_FALSE_CMP) {
                *n_read += 4;
                js       = _lz_j_boolean_new(false);
            }

            break;
        default:
            return NULL;
    } /* switch */

    return js;
}     /* lz_json_parse_boolean */

inline __attribute__((always_inline)) lz_json *
lz_json_parse_null(const char * data, size_t len, size_t * n_read) {
    if (len < 4) {
        return NULL;
    }

    if (!lz_str30_cmp(data, 'n', 'u', 'l', 'l')) {
        return NULL;
    }

    *n_read += 4;

    return _lz_j_null_new();
}

lz_json *
lz_json_parse_value(const char * data, size_t len, size_t * n_read) {
    if (data == NULL || len == 0) {
        /* *n_read = 0; */
        return NULL;
    }

    switch (data[0]) {
        case '"':
            return lz_json_parse_string(data, len, n_read);
        case '{':
            return lz_json_parse_object(data, len, n_read);
        case '[':
            return lz_json_parse_array(data, len, n_read);
        default:
            if (isdigit(data[0])) {
                return lz_json_parse_number(data, len, n_read);
            }

            switch (*data) {
                case 't':
                case 'f':
                    return lz_json_parse_boolean(data, len, n_read);
                case 'n':
                    return lz_json_parse_null(data, len, n_read);
            }
    } /* switch */

    /* *n_read = 0; */
    return NULL;
}

lz_json *
lz_json_parse_array(const char * data, size_t len, size_t * n_read) {
    unsigned char  ch;
    unsigned char  end_ch;
    size_t         i;
    bool           error;
    size_t         b_read;
    lz_j_arr_state state;
    lz_json      * js;


    if (!data || !len || *data != '[') {
        /* *n_read = 0; */
        return NULL;
    }

    data++;
    len--;

    js     = _lz_j_array_new();
    state  = lz_j_arr_s_val;
    error  = false;
    b_read = 0;
    end_ch = 0;

    for (i = 0; i < len; i++) {
        lz_json * val;

        ch = data[i];

        if (isspace(ch)) {
            continue;
        }

        switch (state) {
            case lz_j_arr_s_val:
                if (ch == ']') {
                    end_ch = ch;
                    state  = lz_j_arr_s_end;
                    break;
                }

                if (!(val = lz_json_parse_value(&data[i], (len - i), &b_read))) {
                    error = true;
                    goto end;
                }

                i     += b_read;
                b_read = 0;

                _lz_j_array_add(js, val);

                state  = lz_j_arr_s_comma;

                if ((i + 1) == len) {
                    end_ch = data[i];
                }

                break;
            case lz_j_arr_s_comma:
                switch (ch) {
                    case ',':
                        state  = lz_j_arr_s_val;
                        break;
                    case ']':
                        end_ch = ch;
                        state  = lz_j_arr_s_end;
                        break;
                    default:
                        error  = true;
                        goto end;
                }
                break;
            case lz_j_arr_s_end:
                goto end;
        } /* switch */
    }
end:
    *n_read += i;

    if ((end_ch != ']' || error == true)) {
        lz_safe_free(js, lz_json_free);
        return NULL;
    }

    return js;
} /* lz_json_parse_array */

lz_json *
lz_json_parse_object(const char * data, size_t len, size_t * n_read) {
    unsigned char  ch;
    unsigned char  end_ch;
    size_t         i;
    lz_json      * js;
    lz_json      * key;
    lz_json      * val;
    lz_j_obj_state state;
    bool           error;
    size_t         b_read;

    if (*data != '{') {
        /* *n_read = 0; */
        return NULL;
    }

    state  = lz_j_obj_s_key;
    js     = _lz_j_object_new();
    key    = NULL;
    val    = NULL;
    error  = false;
    b_read = 0;
    end_ch = 0;

    data++;
    len--;

    for (i = 0; i < len; i++) {
        ch = data[i];

        if (isspace(ch)) {
            continue;
        }

        switch (state) {
            case lz_j_obj_s_key:
                if (ch == '}') {
                    end_ch = ch;
                    state  = lz_j_obj_s_end;
                    break;
                }

                if (!(key = lz_json_parse_key(&data[i], (len - i), &b_read))) {
                    error = true;
                    i    += b_read;
                    goto end;
                }

                i     += b_read;
                b_read = 0;
                state  = lz_j_obj_s_delim;
                break;
            case lz_j_obj_s_delim:
                if (ch != ':') {
                    error = true;
                    goto end;
                }

                state = lz_j_obj_s_val;
                break;

            case lz_j_obj_s_val:
                if (!(val = lz_json_parse_value(&data[i], (len - i), &b_read))) {
                    error = true;
                    i    += b_read;
                    goto end;
                }

                i     += b_read;
                b_read = 0;

                _lz_j_object_add(js, key->string, val);

                lz_safe_free(key, lz_json_free);

                key   = NULL;
                state = lz_j_obj_s_comma;

                break;

            case lz_j_obj_s_comma:
                switch (ch) {
                    case ',':
                        state  = lz_j_obj_s_key;
                        break;
                    case '}':
                        end_ch = ch;
                        state  = lz_j_obj_s_end;
                        break;
                    default:
                        error  = true;
                        goto end;
                }
                break;
            case lz_j_obj_s_end:
                goto end;
        } /* switch */
    }

end:
    *n_read += i;

    lz_safe_free(key, lz_json_free);

    if ((end_ch != '}' || error == true)) {
        lz_safe_free(js, lz_json_free);
        return NULL;
    }

    return js;
} /* lz_json_parse_object */

lz_json *
lz_json_parse_buf(const char * data, size_t len, size_t * n_read) {
    unsigned char ch;
    size_t        b_read;
    size_t        i;
    lz_json     * js;
    lz_j_state    state;

    js     = NULL;
    b_read = 0;
    state  = lz_j_s_start;

    for (i = 0; i < len; i++) {
        ch = data[i];

        if (isspace(ch)) {
            continue;
        }

        switch (state) {
            case lz_j_s_start:
                switch (ch) {
                    case '{':
                        if (!(js = lz_json_parse_object(&data[i], (len - i), &b_read))) {
                            *n_read += b_read;
                            return NULL;
                        }

                        i     += b_read;
                        b_read = 0;
                        break;
                    case '[':
                        if (!(js = lz_json_parse_array(&data[i], (len - i), &b_read))) {
                            *n_read += b_read;
                            return NULL;
                        }

                        i       += b_read;
                        b_read   = 0;
                        break;
                    default:
                        *n_read += i;
                        return NULL;
                } /* switch */

                state = lz_j_s_end;
                break;
            case lz_j_s_end:
                break;
        }         /* switch */
    }

    *n_read += i;

    return js;
}         /* lz_json_parse_buf */

lz_json *
lz_json_parse_file(const char * filename, size_t * bytes_read) {
    lz_json * json   = NULL;
    FILE    * fp     = NULL;
    char    * buf    = NULL;
    size_t    n_read = 0;
    long      file_size;

    if (filename == NULL) {
        return NULL;
    }

    do {
        if (!(fp = fopen(filename, "re"))) {
            break;
        }

        if (fseek(fp, 0L, SEEK_END) == -1) {
            break;
        }

        if ((file_size = ftell(fp)) == -1) {
            break;
        }

        if (fseek(fp, 0L, SEEK_SET) == -1) {
            break;
        }

        /* allocate 1 more than the size, just incase there is not an EOL
         * terminator in the file.
         */
        if (!(buf = calloc(file_size + 1, 1))) {
            break;
        }

        if (fread(buf, 1, file_size, fp) != file_size) {
            break;
        }

        if (buf[file_size] == 0) {
            /* just make sure we have SOME type of EOL terminator by placing a
             * \n in it. */
            buf[file_size] = '\n';
            file_size     += 1;
        }

        if (!(json = lz_json_parse_buf(buf, file_size, &n_read))) {
            break;
        }
    } while (0);

    if (fp != NULL) {
        fclose(fp);
    }

    *bytes_read = n_read;

    lz_safe_free(buf, free);
    return json;
} /* lz_json_parse_file */

void
lz_json_free(lz_json * js) {
    if (js == NULL) {
        return;
    }

    switch (_lz_j_type(js)) {
        case lz_json_vtype_string:
            lz_safe_free(js->string, free);
            break;
        case lz_json_vtype_object:
            lz_safe_free(js->object, lz_kvmap_free);
            break;
        case lz_json_vtype_array:
            lz_safe_free(js->array, lz_tailq_free);
            break;
        default:
            break;
    }

    lz_heap_free(__js_heap, js);
}

static inline __attribute__((always_inline)) lz_json *
_array_index(lz_json * array, int offset) {
    lz_tailq * list;

    if (!(list = lz_json_get_array(array))) {
        return NULL;
    }

    return (lz_json*)lz_tailq_get_at_index(list, offset);
}

enum path_state {
    path_state_reading_key,
    path_state_reading_array,
    path_state_reading_array_end
};


inline __attribute__((always_inline)) lz_json *
lz_json_get_array_index(lz_json * array, int offset) {
    if (lz_unlikely(array == NULL || offset < 0)) {
        return NULL;
    }

    return _array_index(array, offset);
}

lz_json *
lz_json_path_get(lz_json * js, const char * path) {
    char            buf[strlen(path) + 1];
    int             buf_idx;
    lz_kvmap      * object;
    lz_json       * prev;
    unsigned char   ch;
    size_t          i;
    enum path_state state;


    if (lz_unlikely(js == NULL || path == NULL)) {
        return NULL;
    }

    prev    = js;
    object  = NULL;
    buf_idx = 0;
    buf[0]  = '\0';
    state   = path_state_reading_key;

    for (i = 0; i < strlen(path) + 1; i++) {
        ch = path[i];

        switch (state) {
            case path_state_reading_key:
                switch (ch) {
                    case '[':
                        state = path_state_reading_array;
                        break;
                    case '\0':
                    case '.':
                        if (!(object = lz_json_get_object(prev))) {
                            return NULL;
                        }

                        if (!(prev = lz_kvmap_find(object, buf))) {
                            return NULL;
                        }

                        buf[0]         = '\0';
                        buf_idx        = 0;
                        break;
                    default:
                        buf[buf_idx++] = ch;
                        buf[buf_idx]   = '\0';
                        break;
                } /* switch */
                break;
            case path_state_reading_array:
                switch (ch) {
                    case ']':
                        if (!(prev = _array_index(prev, lz_atoi(buf, buf_idx)))) {
                            return NULL;
                        }

                        buf[0]         = '\0';
                        buf_idx        = 0;

                        state          = path_state_reading_array_end;
                        break;
                    default:
                        buf[buf_idx++] = ch;
                        buf[buf_idx]   = '\0';
                        break;
                }
                break;
            case path_state_reading_array_end:
                state = path_state_reading_key;
                break;
        } /* switch */

        if (ch == '\0') {
            break;
        }
    }

    return (prev != js) ? prev : NULL;
} /* lz_json_path_get */

inline __attribute__((always_inline)) lz_json *
lz_json_new_object(void) {
    return _lz_j_object_new();
}

inline __attribute__((always_inline)) lz_json *
lz_json_new_array(void) {
    return _lz_j_array_new();
}

inline __attribute__((always_inline)) lz_json *
lz_json_string_new(const char * str) {
    return _lz_j_string_new(str, str ? strlen(str) : 0);
}

inline lz_json *
lz_json_string_new_len(const char * str, size_t size) {
    return _lz_j_string_new(str, size);
}


inline __attribute__((always_inline)) lz_json *
lz_json_number_new(unsigned int num) {
    return _lz_j_number_new(num);
}

inline __attribute__((always_inline)) lz_json *
lz_json_boolean_new(bool boolean) {
    return _lz_j_boolean_new(boolean);
}

inline __attribute__((always_inline)) lz_json *
lz_json_null_new(void) {
    return _lz_j_null_new();
}

inline int
lz_json_object_add(lz_json * obj, const char * key, lz_json * val) {
    if (!obj || !key || !val) {
        return -1;
    }

    return _lz_j_object_add(obj, key, val);
}

inline int
lz_json_object_add_klen(lz_json * obj, const char * k, size_t klen, lz_json * v) {
    if (lz_unlikely(obj == NULL)) {
        return -1;
    }

    return _lz_j_object_add_klen(obj, k, klen, v);
}

inline __attribute__((always_inline)) int
lz_json_array_add(lz_json * array, lz_json * val) {
    return _lz_j_array_add(array, val);
}

int
lz_json_add(lz_json * obj, const char * key, lz_json * val) {
    if (!obj) {
        return -1;
    }

    if (key == NULL) {
        if (_lz_j_type(obj) != lz_json_vtype_array) {
            return -1;
        }

        return lz_json_array_add(obj, val);
    }

    return lz_json_object_add(obj, key, val);
}

struct __jbuf {
    char  * buf;
    size_t  buf_idx;
    size_t  buf_len;
    ssize_t written;
    int     dynamic;
    bool    escape;
};

static inline int
_addbuf(struct __jbuf * jbuf, const char * buf, size_t len) {
    lz_assert(jbuf != NULL);

    if (len == 0 || buf == NULL) {
        return 0;
    }

    if ((jbuf->buf_idx + len) > jbuf->buf_len) {
        if (lz_unlikely(jbuf->dynamic == 1)) {
            char * n_buf;

            jbuf->buf      = realloc(jbuf->buf, (size_t)(jbuf->buf_len + len + 32));
            lz_alloc_assert(jbuf->buf);

            jbuf->buf_len += len + 32;
        } else {
            return -1;
        }
    }

    memcpy(&jbuf->buf[jbuf->buf_idx], buf, len);

    jbuf->buf_idx += len;
    jbuf->written += len;

    return 0;
}

static int
_addbuf_vprintf(struct __jbuf * jbuf, const char * fmt, va_list ap) {
    char tmpbuf[jbuf->buf_len - jbuf->buf_idx];
    int  sres;

    lz_assert(jbuf != NULL);

    sres = vsnprintf(tmpbuf, sizeof(tmpbuf), fmt, ap);

    if (sres >= sizeof(tmpbuf) || sres < 0) {
        return -1;
    }

    return _addbuf(jbuf, tmpbuf, (size_t)sres);
}

static int
_addbuf_printf(struct __jbuf * jbuf, const char * fmt, ...) {
    va_list ap;
    int     sres;

    lz_assert(jbuf != NULL);

    va_start(ap, fmt);
    {
        sres = _addbuf_vprintf(jbuf, fmt, ap);
    }
    va_end(ap);

    return sres;
}

static const char digits[] =
    "0001020304050607080910111213141516171819"
    "2021222324252627282930313233343536373839"
    "4041424344454647484950515253545556575859"
    "6061626364656667686970717273747576777879"
    "8081828384858687888990919293949596979899";

static int
_addbuf_number(struct __jbuf * jbuf, unsigned int num) {
    char     buf[32]; /* 18446744073709551615 64b, 20 characters */
    char   * buffer          = (char*)buf;
    char   * buffer_end      = buffer + 32;
    char   * buffer_end_save = buffer + 32;
    unsigned index;

    lz_assert(jbuf != NULL);

    *--buffer_end = '\0';

    while (num >= 100) {
        index         = (num % 100) * 2;

        num          /= 100;

        *--buffer_end = digits[index + 1];
        *--buffer_end = digits[index];
    }

    if (num < 10) {
        *--buffer_end = (char)('0' + num);
    } else {
        index         = (unsigned)(num * 2);

        *--buffer_end = digits[index + 1];
        *--buffer_end = digits[index];
    }

    return _addbuf(jbuf, buffer_end, (size_t)(buffer_end_save - buffer_end - 1));
}

static int _lz_j_array_to_buffer(lz_json * json, struct __jbuf * jbuf);
static int _lz_j_number_to_buffer(lz_json * json, struct __jbuf * jbuf);
static int _lz_j_object_to_buffer(lz_json * json, struct __jbuf * jbuf);

static int
_lz_j_number_to_buffer(lz_json * json, struct __jbuf * jbuf) {
    if (lz_likely(_lz_j_type(json) != lz_json_vtype_number)) {
        return -1;
    } else {
        return _addbuf_number(jbuf, json->number);
    }
}

static int
_lz_j_escape_string(const char * str, size_t len, struct __jbuf * jbuf) {
    unsigned char ch;
    size_t        i;

    lz_assert(jbuf != NULL);

    if (lz_unlikely(str == NULL)) {
        return -1;
    }

    for (i = 0; i < len; i++) {
        ch = str[i];

        switch (ch) {
            default:
                if (lz_unlikely(_addbuf(jbuf, (const char*)&ch, 1) == -1)) {
                    return -1;
                }
                break;
            case '\n':
                if (lz_unlikely(_addbuf(jbuf, "\\n", 2) == -1)) {
                    return -1;
                }
                break;
            case '"':
                if (lz_unlikely(_addbuf(jbuf, "\\\"", 2) == -1)) {
                    return -1;
                }
                break;
            case '\t':
                if (lz_unlikely(_addbuf(jbuf, "\\t", 2) == -1)) {
                    return -1;
                }
                break;
            case '\r':
                if (lz_unlikely(_addbuf(jbuf, "\\r", 2) == -1)) {
                    return -1;
                }
                break;
            case '\\':
                if (lz_unlikely(_addbuf(jbuf, "\\\\", 2) == -1)) {
                    return -1;
                }
                break;
        } /* switch */
    }

    return 0;
}         /* _lz_j_escape_string */

static int
_lz_j_string_to_buffer(lz_json * json, struct __jbuf * jbuf) {
    const char * str;

    if (_lz_j_type(json) != lz_json_vtype_string) {
        return -1;
    }

    str = json->string;

    if (lz_unlikely(str == NULL)) {
        return -1;
    }

    if (lz_unlikely(_addbuf(jbuf, "\"", 1) == -1)) {
        return -1;
    }

    if (jbuf->escape == true) {
        if (lz_unlikely(_lz_j_escape_string(str, json->slen, jbuf) == -1)) {
            return -1;
        }
    }

    return _addbuf(jbuf, "\"", 1);
}

static int
_lz_j_boolean_to_buffer(lz_json * json, struct __jbuf * jbuf) {
    if (_lz_j_type(json) != lz_json_vtype_bool) {
        return -1;
    }

    return _addbuf_printf(jbuf, "%s",
                          lz_json_get_boolean(json) == true ? "true" : "false");
}

static int
_lz_j_null_to_buffer(lz_json * json, struct __jbuf * jbuf) {
    if (_lz_j_type(json) != lz_json_vtype_null) {
        return -1;
    }

    return _addbuf_printf(jbuf, "null");
}

static int
_lz_j_to_buffer(lz_json * json, struct __jbuf * jbuf) {
    switch (_lz_j_type(json)) {
        case lz_json_vtype_number:
            return _lz_j_number_to_buffer(json, jbuf);
        case lz_json_vtype_array:
            return _lz_j_array_to_buffer(json, jbuf);
        case lz_json_vtype_object:
            return _lz_j_object_to_buffer(json, jbuf);
        case lz_json_vtype_string:
            return _lz_j_string_to_buffer(json, jbuf);
        case lz_json_vtype_bool:
            return _lz_j_boolean_to_buffer(json, jbuf);
        case lz_json_vtype_null:
            return _lz_j_null_to_buffer(json, jbuf);
        default:
            return -1;
    }

    return 0;
}

static int
_lz_j_array_to_buffer(lz_json * json, struct __jbuf * jbuf) {
    lz_tailq      * array;
    lz_tailq_elem * elem;
    lz_tailq_elem * temp;

    if (_lz_j_type(json) != lz_json_vtype_array) {
        return -1;
    }

    array = json->array;

    if (lz_unlikely(_addbuf(jbuf, "[", 1) == -1)) {
        return -1;
    }

    for (elem = lz_tailq_first(array); elem; elem = temp) {
        lz_json * val;

        val = (lz_json*)lz_tailq_elem_data(elem);
        lz_assert(val != NULL);

        if (lz_unlikely(_lz_j_to_buffer(val, jbuf) == -1)) {
            return -1;
        }

        if ((temp = lz_tailq_next(elem))) {
            if (lz_unlikely(_addbuf(jbuf, ",", 1) == -1)) {
                return -1;
            }
        }
    }

    if (lz_unlikely(_addbuf(jbuf, "]", 1) == -1)) {
        return -1;
    }

    return 0;
}

static int
_lz_j_object_to_buffer(lz_json * json, struct __jbuf * jbuf) {
    lz_kvmap     * object;
    lz_kvmap_ent * ent;
    lz_kvmap_ent * temp;

    if (_lz_j_type(json) != lz_json_vtype_object) {
        return -1;
    }

    object = json->object;

    if (lz_unlikely(_addbuf(jbuf, "{", 1) == -1)) {
        return -1;
    }

    for (ent = lz_kvmap_first(object); ent; ent = temp) {
        const char * key;
        lz_json    * val;

        key = lz_kvmap_ent_key(ent);
        lz_assert(key != NULL);

        val = (lz_json*)lz_kvmap_ent_val(ent);
        lz_assert(val != NULL);

        if (lz_unlikely(_addbuf(jbuf, "\"", 1) == -1)) {
            return -1;
        }

        if (lz_unlikely(_addbuf(jbuf, key, lz_kvmap_ent_get_klen(ent)) == -1)) {
            return -1;
        }

        if (lz_unlikely(_addbuf(jbuf, "\":", 2) == -1)) {
            return -1;
        }

        if (lz_unlikely(_lz_j_to_buffer(val, jbuf) == -1)) {
            return -1;
        }

        if ((temp = lz_kvmap_next(ent))) {
            if (lz_unlikely(_addbuf(jbuf, ",", 1) == -1)) {
                return -1;
            }
        }
    }

    if (lz_unlikely(_addbuf(jbuf, "}", 1) == -1)) {
        return -1;
    }

    return 0;
} /* _lz_j_object_to_buffer */

ssize_t
_lz_json_to_buffer(lz_json * json, char * buf, size_t buf_len, struct __jbuf * jbuf) {
    if (lz_unlikely(!json || !buf)) {
        return -1;
    }

    if (lz_unlikely(_lz_j_to_buffer(json, jbuf) == -1)) {
        return -1;
    }

    return jbuf->written;
}

ssize_t
lz_json_to_buffer(lz_json * json, char * buf, size_t buf_len) {
    struct __jbuf jbuf = {
        .buf     = buf,
        .buf_idx = 0,
        .written = 0,
        .buf_len = buf_len,
        .dynamic = 0,
        .escape  = true
    };

    if (lz_unlikely(_lz_j_to_buffer(json, &jbuf) == -1)) {
        return -1;
    }

    return jbuf.written;
}

ssize_t
lz_json_to_buffer_nescp(lz_json * json, char * buf, size_t buf_len) {
    struct __jbuf jbuf = {
        .buf     = buf,
        .buf_idx = 0,
        .written = 0,
        .buf_len = buf_len,
        .dynamic = 0,
        .escape  = false
    };

    if (lz_unlikely(_lz_j_to_buffer(json, &jbuf) == -1)) {
        return -1;
    }

    return jbuf.written;
}

char *
lz_json_to_buffer_alloc(lz_json * json, size_t * len) {
    struct __jbuf jbuf = {
        .buf     = NULL,
        .buf_idx = 0,
        .written = 0,
        .buf_len = 0,
        .dynamic = 1,
        .escape  = true
    };

    if (!json || !len) {
        return NULL;
    }

    if (_lz_j_to_buffer(json, &jbuf) == -1) {
        lz_safe_free(jbuf.buf, free);
        return NULL;
    }

    *len = jbuf.written;

    return jbuf.buf;
}

static inline int _lz_j_compare(lz_json * j1, lz_json * j2, lz_json_key_filtercb cb);

static inline int
_lz_j_number_compare(lz_json * j1, lz_json * j2, lz_json_key_filtercb cb) {
    if (j1 == NULL || j2 == NULL) {
        return -1;
    }

    if (_lz_j_type(j1) != lz_json_vtype_number) {
        return -1;
    }

    if (_lz_j_type(j2) != lz_json_vtype_number) {
        return -1;
    }

    if (lz_json_get_number(j1) != lz_json_get_number(j2)) {
        return -1;
    }

    return 0;
}

static inline int
_lz_j_array_compare(lz_json * j1, lz_json * j2, lz_json_key_filtercb cb) {
    lz_tailq      * j1_array;
    lz_tailq      * j2_array;
    lz_tailq_elem * elem;
    lz_tailq_elem * temp;
    int             idx;

    if (j1 == NULL || j2 == NULL) {
        return -1;
    }

    if (!(j1_array = lz_json_get_array(j1))) {
        return -1;
    }

    if (!(j2_array = lz_json_get_array(j2))) {
        return -1;
    }

    idx = 0;

    for (elem = lz_tailq_first(j1_array); elem; elem = temp) {
        lz_json * j1_val;
        lz_json * j2_val;

        j1_val = (lz_json*)lz_tailq_elem_data(elem);
        j2_val = (lz_json*)lz_tailq_get_at_index(j2_array, idx);

        if (j1_val && !j2_val) {
            return -1;
        }

        if (j2_val && !j1_val) {
            return -1;
        }

        if (_lz_j_compare(j1_val, j2_val, cb) == -1) {
            return -1;
        }

        idx += 1;

        temp = lz_tailq_next(elem);
    }

    return 0;
} /* _lz_j_array_compare */

static inline __attribute__((always_inline)) int
_lz_j_object_compare(lz_json * j1, lz_json * j2, lz_json_key_filtercb cb) {
    lz_kvmap     * j1_map;
    lz_kvmap     * j2_map;
    lz_kvmap_ent * ent;
    lz_kvmap_ent * temp;

    if (j1 == NULL || j2 == NULL) {
        return -1;
    }

    if (!(j1_map = lz_json_get_object(j1))) {
        return -1;
    }

    if (!(j2_map = lz_json_get_object(j2))) {
        return -1;
    }

    for (ent = lz_kvmap_first(j1_map); ent; ent = temp) {
        const char * key;
        lz_json    * j1_val;
        lz_json    * j2_val;

        if (!(key = lz_kvmap_ent_key(ent))) {
            return -1;
        }

        if (!(j1_val = (lz_json*)lz_kvmap_ent_val(ent))) {
            return -1;
        }

        if (cb && (cb)(key, j1_val) == 1) {
            /* the key filter callback returned 1, which means we can ignore the
             * comparison of this field.
             */
            temp = lz_kvmap_next(ent);
            continue;
        }

        if (!(j2_val = (lz_json*)lz_kvmap_find(j2_map, key))) {
            return -1;
        }

        if (_lz_j_compare(j1_val, j2_val, cb) == -1) {
            return -1;
        }

        temp = lz_kvmap_next(ent);
    }

    return 0;
} /* _lz_j_object_compare */

static inline __attribute__((always_inline)) int
_lz_j_string_compare(lz_json * j1, lz_json * j2, lz_json_key_filtercb cb) {
    const char * j1_str;
    const char * j2_str;

    if (!(j1_str = lz_json_get_string(j1))) {
        return -1;
    }

    if (!(j2_str = lz_json_get_string(j2))) {
        return -1;
    }

    if (strcmp(j1_str, j2_str)) {
        return -1;
    }

    return 0;
}

static inline __attribute__((always_inline)) int
_lz_j_boolean_compare(lz_json * j1, lz_json * j2, lz_json_key_filtercb cb) {
    if (!j1 || !j2) {
        return -1;
    }

    if (_lz_j_type(j1) != lz_json_vtype_bool) {
        return -1;
    }

    if (_lz_j_type(j2) != lz_json_vtype_bool) {
        return -1;
    }

    if (lz_json_get_boolean(j1) != lz_json_get_boolean(j2)) {
        return -1;
    }

    return 0;
}

static inline __attribute__((always_inline)) int
_lz_j_null_compare(lz_json * j1, lz_json * j2, lz_json_key_filtercb cb) {
    if (j1 == NULL || j2 == NULL) {
        return -1;
    }

    if (_lz_j_type(j1) != lz_json_vtype_null
        || _lz_j_type(j2) != lz_json_vtype_null) {
        return -1;
    }

    return 0;
}

static inline int
_lz_j_compare(lz_json * j1, lz_json * j2, lz_json_key_filtercb cb) {
    if (j1 == NULL || j2 == NULL) {
        return -1;
    }

    if (_lz_j_type(j1) != _lz_j_type(j2)) {
        return -1;
    }

    if (lz_json_get_size(j1) != lz_json_get_size(j2)) {
        return -1;
    }

    switch (_lz_j_type(j1)) {
        case lz_json_vtype_number:
            return _lz_j_number_compare(j1, j2, cb);
        case lz_json_vtype_array:
            return _lz_j_array_compare(j1, j2, cb);
        case lz_json_vtype_object:
            return _lz_j_object_compare(j1, j2, cb);
        case lz_json_vtype_string:
            return _lz_j_string_compare(j1, j2, cb);
        case lz_json_vtype_bool:
            return _lz_j_boolean_compare(j1, j2, cb);
        case lz_json_vtype_null:
            return _lz_j_null_compare(j2, j2, cb);
        default:
            return -1;
    }

    return 0;
}

inline int
lz_json_compare(lz_json * j1, lz_json * j2, lz_json_key_filtercb cb) {
    return _lz_j_compare(j1, j2, cb);
}

#if 0
static lz_json *
_lz_j_array_copy(lz_json * in, lz_json * out) {
    lz_tailq      * array;
    lz_tailq_elem * elem;
    lz_tailq_elem * temp;

    if (_lz_j_type(in) != lz_json_vtpe_array) {
        return NULL;
    }

    array = in->array;

    for (elem = lz_tailq_first(array); elem; elem = temp) {
        lz_json * val;

        val = (lz_json*)lz_tailq_elem_data(elem);
        lz_assert(val != NULL);
    }
}

static lz_json *
_lz_j_copy(lz_json * in, lz_json ** out) {
#if 0
    if (*out == NULL) {
        *out = _lz_j_new(_lz_j_type(in));
        lz_alloc_assert(*out);
    }
#endif

    switch (_lz_j_type(in)) {
        case lz_json_vtype_number:
            return _lz_j_number_copy(in, out);
        case lz_json_vtype_array:
            return _lz_j_array_copy(in, out);
        case lz_json_vtype_object:
            return _lz_j_object_copy(in, out);
        case lz_json_vtype_string:
            return _lz_j_string_copy(in, out);
        case lz_json_vtype_bool:
            return _lz_j_boolean_copy(in, out);
        case lz_json_vtype_null:
            return _lz_j_null_copy(in, out);
        default:
            break;
    }

    return NULL;
}

lz_json *
lz_json_copy(lz_json * in) {
    lz_json * out;

    if (lz_unlikely(in == NULL)) {
        return NULL;
    }

    out = NULL;

    return _lz_j_copy(in, &out);
}

#endif
