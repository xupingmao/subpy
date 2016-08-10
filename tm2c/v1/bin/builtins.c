#include "include/tm.h"
#include <ctype.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>

#ifdef TM_NT
    #include <windows.h>
#else
    #include <unistd.h>
#endif

Object* get_builtin(char* key) {
    if (!tm->init) {
        return NULL;
    }
    return dict_get_by_str(tm->builtins, key);
}

void tm_putchar(int c){
    static char hex[] = {
        '0','1','2','3','4','5','6','7',
        '8','9','A','B','C','D','E','F'
    };

    if (isprint(c) || c == '\n' || c == '\t') {
        putchar(c);
    } else if(c == '\b') {
        putchar('\b');
    } else if(c=='\r'){
        /* output nothing */
    } else {
        int c0 = (c & 0xf0) >> 4;
        int c1 = c & 0x0f;
        printf("0x%c%c", hex[c0], hex[c1]);
    }
}

void tm_print(Object o) {
    Object str = tm_str(o);
    int i;
    for(i = 0; i < GET_STR_LEN(str); i++) {
        tm_putchar(GET_STR(str)[i]);
    }
}

void tm_println(Object o) {
    tm_print(o);
    puts("");
}



/**
 * based on C language standard.
 * d  -> int
 * f  -> double
 * c  -> char
 * s  -> char*
 * p  -> pointer
 * o  -> repl(object)
 * os -> object
 */
Object tm_format_va_list(char* fmt, va_list ap, int acquire_new_line) {
    int i;
    int len = strlen(fmt);
    Object str = string_new("");
    int templ = 0;
    char* start = fmt;
    int istrans = 1;
    char buf[20];
    for (i = 0; i < len; i++) {
        if (fmt[i] == '%') {
            i++;
            switch (fmt[i]) {
            case 'd':
                sprintf(buf, "%d", va_arg(ap, int));
                str = string_append_sz(str, buf);
                break;
            case 'f':
                /* ... will pass float as double */
                sprintf(buf, "%lf", va_arg(ap, double));
                str = string_append_sz(str, buf);
                break;
                /* ... will pass char  as int */
            case 'c':
                str = string_append_char(str, va_arg(ap, int));
                break;
            case 's': {
                str = string_append_sz(str, va_arg(ap, char*));
                break;
            }
            case 'P':
            case 'p': {
                sprintf(buf, "%p", va_arg(ap, void*));
                str = string_append_sz(str, buf);
                break;
            }
            case 'o': {
                int next = fmt[i+1];
                Object v = va_arg(ap, Object);
                if (IS_STR(v) && next != 's') {
                    str = string_append_char(str, '"');
                }
                str = string_append_obj(str, v);
                if (IS_STR(v) && next != 's') {
                    str = string_append_char(str, '"');
                }
                if (next == 's') {
                    i++;
                }
                break;
            }
            default:
                tm_raise("format, unknown pattern %c", fmt[i]);
                break;
            }
        } else {
            str = string_append_char(str, fmt[i]);
        }
    }
    if (acquire_new_line) {
        str = string_append_char(str, '\n');
    }
    return str;
}

Object tm_format(char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    Object v = tm_format_va_list(fmt, a, 0);
    va_end(a);
    return v;
}

void tm_printf(char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    tm_print(tm_format_va_list(fmt, a, 0));
    va_end(a);
}


long get_rest_size(FILE* fp){
    long cur, end;
    cur = ftell(fp);
    fseek(fp, 0, SEEK_END);
    end = ftell(fp);
    fseek(fp, cur, SEEK_SET);
    return end - cur;
}


// vm-builtins

Object tm_load(char* fname){
    FILE* fp = fopen(fname, "rb");
    if(fp == NULL){
        tm_raise("load: can not open file \"%s\"",fname);
        return NONE_OBJECT;
    }
    long len = get_rest_size(fp);
    if(len > MAX_FILE_SIZE){
        tm_raise("load: file too big to load, size = %d", (len));
        return NONE_OBJECT;
    }
    Object text = string_alloc(NULL, len);
    char* s = GET_STR(text);
    fread(s, 1, len, fp);
    fclose(fp);
    return text;
}

Object tm_save(char*fname, Object content) {
    FILE* fp = fopen(fname, "wb");
    if (fp == NULL) {
        tm_raise("tm_save : can not save to file \"%s\"", fname);
    }
    char* txt = GET_STR(content);
    int len = GET_STR_LEN(content);
    fwrite(txt, 1, len, fp);
    fclose(fp);
    return NONE_OBJECT;
}


Object bf_input() {
    int i = 0;
    if (has_arg()) {
        tm_print(arg_take_obj("input"));
    }
    char buf[2048];
    memset(buf, '\0', sizeof(buf));
    fgets(buf, sizeof(buf), stdin);
    int len = strlen(buf);
    /* if last char is '\n', we shift it, mainly in tcc */
    if(buf[len-1]=='\n'){
        buf[len-1] = '\0';
    }
    return string_new(buf);
}

Object bf_int() {
    Object v = arg_take_obj("int");
    if (v.type == TYPE_NUM) {
        return tm_number((int) GET_NUM(v));
    } else if (v.type == TYPE_STR) {
        return tm_number((int) atof(GET_STR(v)));
    }
    tm_raise("int: %o can not be parsed to int ", v);
    return NONE_OBJECT;
}

Object bf_float() {
    Object v = arg_take_obj("float");
    if (v.type == TYPE_NUM) {
        return v;
    } else if (v.type == TYPE_STR) {
        return tm_number(atof(GET_STR(v)));
    }
    tm_raise("float: %o can not be parsed to float", v);
    return NONE_OBJECT;
}

/**
 *   load_module( name, code, mod_name = None )
 */
Object bf_load_module() {
    const char* sz_fnc = "load_module";
    Object file = arg_take_str_obj(sz_fnc);
    Object code = arg_take_str_obj(sz_fnc);
    Object mod;
    if (get_args_count() == 3) {
        mod = module_new(file, arg_take_str_obj(sz_fnc), code);
    } else {
        mod = module_new(file, file, code);
    }
    Object fnc = func_new(mod, NONE_OBJECT, NULL);
    GET_FUNCTION(fnc)->code = (unsigned char*) GET_STR(code);
    GET_FUNCTION(fnc)->name = string_new("#main");
    call_function(fnc);
    return GET_MODULE(mod)->globals;
}


/* get globals */
Object bf_globals() {
    return GET_FUNCTION_GLOBALS(tm->frame->fnc);
}

/* get object type */

Object bf_exit() {
    longjmp(tm->frames->buf, 2);
    return NONE_OBJECT;
}

Object bf_gettype() {
    Object obj = arg_take_obj("gettype");
    switch(TM_TYPE(obj)) {
        case TYPE_STR: return sz_to_string("string");
        case TYPE_NUM: return sz_to_string("number");
        case TYPE_LIST: return sz_to_string("list");
        case TYPE_DICT: return sz_to_string("dict");
        case TYPE_FUNCTION: return sz_to_string("function");
        case TYPE_DATA: return sz_to_string("data");
        case TYPE_NONE: return sz_to_string("None");
        default: tm_raise("gettype(%o)", obj);
    }
    return NONE_OBJECT;
}

/**
 * bool istype(obj, str_type);
 * this function is better than `gettype(obj) == 'string'`;
 * think that you want to check a `basetype` which contains both `number` and `string`;
 * so, a check function with less result is better.
 */
Object bf_istype() {
    Object obj = arg_take_obj("istype");
    char* type = arg_take_sz("istype");
    int is_type = 0;
    switch(TM_TYPE(obj)) {
        case TYPE_STR: is_type = strcmp(type, "string") == 0 ; break;
        case TYPE_NUM: is_type = strcmp(type, "number") == 0 ; break;
        case TYPE_LIST: is_type = strcmp(type, "list") == 0; break;
        case TYPE_DICT: is_type = strcmp(type, "dict") == 0; break;
        case TYPE_FUNCTION: is_type = strcmp(type, "function") == 0;break;
        case TYPE_DATA: is_type = strcmp(type, "data") == 0; break;
        case TYPE_NONE: is_type = strcmp(type, "None") == 0; break;
        default: tm_raise("gettype(%o)", obj);
    }
    return tm_number(is_type);
}

Object bf_chr() {
    int n = arg_take_int("chr");
    return string_chr(n);
}

Object bf_ord() {
    Object c = arg_take_str_obj("ord");
    TM_ASSERT(GET_STR_LEN(c) == 1, "ord() expected a character");
    return tm_number((unsigned char) GET_STR(c)[0]);
}

Object bf_code8() {
    int n = arg_take_int("code8");
    if (n < 0 || n > 255)
        tm_raise("code8(): expect number 0-255, but see %d", n);
    return string_chr(n);
}

Object bf_code16() {
    int n = arg_take_int("code16");
    if (n < 0 || n > 0xffff)
        tm_raise("code16(): expect number 0-0xffff, but see %x", n);
    Object nchar = string_alloc(NULL, 2);
    code16((unsigned char*) GET_STR(nchar), n);
    return nchar;
}

Object bf_code32() {
    int n = arg_take_int("code32");
    Object c = string_alloc(NULL, 4);
    code32((unsigned char*) GET_STR(c), n);
    return c;
}

Object bf_raise() {
    if (get_args_count() == 0) {
        tm_raise("raise");
    } else {
        tm_raise("%s", arg_take_sz("raise"));
    }
    return NONE_OBJECT;
}

Object bf_system() {
    Object m = arg_take_str_obj("system");
    int rs = system(GET_STR(m));
    return tm_number(rs);
}

Object bf_str() {
    Object a = arg_take_obj("str");
    return tm_str(a);
}

Object bf_len() {
    Object o = arg_take_obj("len");
    return tm_number(tm_len(o));
}

Object bf_print() {
    int i = 0;
    while (has_arg()) {
        tm_print(arg_take_obj("print"));
        if (has_arg()) {
            putchar(' ');
        }
    }
    putchar('\n');
    return NONE_OBJECT;
}

Object bf_load(Object p){
    Object fname = arg_take_str_obj("load");
    return tm_load(GET_STR(fname));
}
Object bf_save(){
    Object fname = arg_take_str_obj("<save name>");
    return tm_save(GET_STR(fname), arg_take_str_obj("<save content>"));
}

Object bf_file_append() {
    char* fname = arg_take_sz("file_append");
    Object content = arg_take_str_obj("file_append");
    FILE* fp = fopen(fname, "ab+");
    if (fp == NULL) {
        tm_raise("file_append: fail to open file %s", fname);
        return NONE_OBJECT;
    }
    char* txt = GET_STR(content);
    int len = GET_STR_LEN(content);
    fwrite(txt, 1, len, fp);
    fclose(fp);
    return NONE_OBJECT;
}

Object bf_remove(){
    Object fname = arg_take_str_obj("remove");
    int flag = remove(GET_STR(fname));
    if(flag) {
        return tm_number(0);
    } else {
        return tm_number(1);
    }
}

Object bf_apply() {
    Object func = arg_take_obj("apply");
    if (NOT_FUNC(func) && NOT_DICT(func)) {
        tm_raise("apply: expect function or dict");
    }
    Object args = arg_take_obj("apply");
    arg_start();
    if (IS_NONE(args)) {
    } else if(IS_LIST(args)) {
        int i;for(i = 0; i < LIST_LEN(args); i++) {
            arg_push(LIST_NODES(args)[i]);
        }
    } else {
        tm_raise("apply: expect list arguments or None, but see %o", args);
        return NONE_OBJECT;
    }
    return call_function(func);
}

Object bf_write() {
    Object fmt = arg_take_obj("write");
    Object str = tm_str(fmt);
    char* s = GET_STR(str);
    int len = GET_STR_LEN(str);
    int i;
    // for(i = 0; i < len; i++) {
        // tm_putchar(s[i]);
        // putchar is very very slow
        // when print 80 * 30 chars  
        //     ==>  putchar_time=126, printf_time=2, putc_time=129
        // putchar(s[i]);
        // buffer[i] = s[i];
    // }
    printf("%s", s);
    // return array_to_list(2, tm_number(t2-t1), tm_number(t3-t2));
    return NONE_OBJECT;
}

Object bf_pow() {
    double base = arg_take_double("pow");
    double y = arg_take_double("pow");
    return tm_number(pow(base, y));
}


Object* range_next(TmData* data) {
    long cur = data->cur;
    if (data->inc > 0 && cur < data->end) {
        data->cur += data->inc;
        data->cur_obj = tm_number(cur);
        return &data->cur_obj;
    } else if (data->inc < 0 && cur > data->end) {
        data->cur += data->inc;
        data->cur_obj = tm_number(cur);
        return &data->cur_obj;
    }
    return NULL;
}

Object bf_range() {
    long start = 0;
    long end = 0;
    int inc;
    static const char* sz_func = "range";
    switch (tm->arg_cnt) {
    case 1:
        start = 0;
        end = (long)arg_take_double(sz_func);
        inc = 1;
        break;
    case 2:
        start = (long)arg_take_double(sz_func);
        end = (long)arg_take_double(sz_func);
        inc = 1;
        break;
    case 3:
        start = (long)arg_take_double(sz_func);
        end   = (long)arg_take_double(sz_func);
        inc   = (long)arg_take_double(sz_func);
        break;
    default:
        tm_raise("range([n, [ n, [n]]]), but see %d arguments",
                tm->arg_cnt);
    }
    if (inc == 0)
        tm_raise("range(): increment can not be 0!");
    if (inc * (end - start) < 0)
        tm_raise("range(%d, %d, %d): not valid range!", start, end, inc);
    Object data = data_new(0);
    TmData *iterator = GET_DATA(data);
    iterator->cur  = start;
    iterator->end  = end;
    iterator->inc  = inc;
    iterator->next = range_next;

    return data;
}

Object* enumerate_next(TmData* iterator) {
    Object iter = iterator->data_ptr[0];
    Object* next_value = next_ptr(iter);

    if (next_value == NULL) {
        return NULL;
    } else {
        int idx = iterator->cur;
        iterator->cur += 1;
        iterator->cur_obj = array_to_list(2, tm_number(idx), *next_value);
        return &iterator->cur_obj;
    }
}

Object bf_enumerate() {
    Object _it = arg_take_obj("enumerate");
    Object origin_iter = iter_new(_it);

    Object data = data_new(1);
    TmData* iterator = GET_DATA(data);
    iterator->cur = 0;
    iterator->data_ptr[0] = origin_iter;
    iterator->next = enumerate_next;
    return data;
}

Object bf_mmatch() {
    char* str = arg_take_sz("mmatch");
    int start = arg_take_int("mmatch");
    Object o_dst = arg_take_str_obj("mmatch");
    char* dst = GET_STR(o_dst);
    int size = GET_STR_LEN(o_dst);
    return tm_number(strncmp(str+start, dst, size) == 0);
}


/***********************************
* built-in functions for developers
***********************************/

Object bf_inspect_ptr() {
    double _ptr = arg_take_double("inspect_ptr");
    int idx = arg_take_int("inspect_ptr");
    char* ptr = (char*)(long long)_ptr;
    return string_chr(ptr[idx]);
}

Object bf_get_current_frame() {
    Object frame_info = dict_new();
    dict_set_by_str(frame_info, "function", tm->frame->fnc);
    // dict_set_by_str(frame_info, "pc", tm_number((long long)tm->frame->pc));
    dict_set_by_str(frame_info, "index", tm_number((long long) (tm->frame - tm->frames)));
    return frame_info;
}

Object bf_get_vm_info() {
    Object tm_info = dict_new();
    dict_set_by_str(tm_info, "name", string_new("tm"));
    dict_set_by_str(tm_info, "vm_size", tm_number(sizeof(TmVm)));
    dict_set_by_str(tm_info, "obj_size", tm_number(sizeof(Object)));
    dict_set_by_str(tm_info, "int_size", tm_number(sizeof(int)));
    dict_set_by_str(tm_info, "long_size", tm_number(sizeof(long)));
    dict_set_by_str(tm_info, "long_long_size", tm_number(sizeof(long long)));
    dict_set_by_str(tm_info, "float_size", tm_number(sizeof(float)));
    dict_set_by_str(tm_info, "double_size", tm_number(sizeof(double)));
    dict_set_by_str(tm_info, "jmp_buf_size", tm_number(sizeof(jmp_buf)));
    dict_set_by_str(tm_info, "total_obj_len", tm_number(tm->all->len));
    dict_set_by_str(tm_info, "alloc_mem", tm_number(tm->allocated));
    dict_set_by_str(tm_info, "gc_threshold", tm_number(tm->gc_threshold));
    dict_set_by_str(tm_info, "frame_index", tm_number(tm->frame - tm->frames));
    dict_set_by_str(tm_info, "consts_len", tm_number(DICT_LEN(tm->constants)));
    return tm_info;
}

long tm_clock() {
#ifdef TM_NT
    return clock();
#else
    return (double)clock()/1000;
#endif
}


Object bf_clock() {
    return tm_number(tm_clock());
}

Object bf_time0() {
    return tm_number(time(0));
}

Object bf_sleep() {
    int i = 0;
    int t = arg_take_int("sleep");
#ifdef _WINDOWS_H
    Sleep(t);
#else
    sleep(t);
#endif
    return NONE_OBJECT;
}

Object bf_add_obj_method() {
    static const char* sz_func = "add_obj_method";
    Object type = arg_take_str_obj(sz_func);
    Object fname = arg_take_str_obj(sz_func);
    Object fnc = arg_take_func_obj(sz_func);
    char*s = GET_STR(type);
    if (strcmp(s, "str") == 0) {
        obj_set(tm->str_proto, fname, fnc);
    } else if (strcmp(s, "list") == 0) {
        obj_set(tm->list_proto, fname, fnc);
    } else if (strcmp(s, "dict") == 0) {
        obj_set(tm->dict_proto, fname, fnc);
    } else {
        tm_raise("add_obj_method: not valid object type, expect str, list, dict");
    }
    return NONE_OBJECT;
}

Object bf_read_file() {
    static const char* sz_func = "read_file";
    char c;
    char* fname = arg_take_sz(sz_func);
    int nsize = arg_take_int(sz_func);
    char buf[1024];
    int i;
    int end = 0;
    Object func;
    if (nsize < 0 || nsize > 1024) {
        tm_raise("%s: can not set bufsize beyond [1, 1024]",  sz_func);
    }
    func = arg_take_func_obj(sz_func);
    FILE* fp = fopen(fname, "rb");
    if (fp == NULL) {
        tm_raise("%s: can not open file %s", sz_func, fname);
    }
    while (1) {
        arg_start();
        for (i = 0; i < nsize; i++) {
            if ((c = fgetc(fp)) != EOF) {
                buf[i] = c;
            } else {
                end = 1;
                break;
            }
        }
        arg_push(string_alloc(buf, i));
        call_function(func);
        if (end) {
            break;
        }
    }
    fclose(fp);
    return NONE_OBJECT;
}

Object bf_iter() {
    Object func = arg_take_obj("iter");
    return iter_new(func);
}

Object bf_next() {
    Object iter = arg_take_data_obj("next");
    Object *ret = next_ptr(iter);
    if (ret == NULL) {
        tm_raise("<<next end>>");
        return NONE_OBJECT;
    } else {
        return *ret;
    }
}

Object bf_set_vm_state() {
    int state = arg_take_int("set_v_m_state");
    switch(state) {
        case 0:tm->debug = 0;break;
        case 1:tm->debug = 1;break;
    }
    return NONE_OBJECT;
}

Object bf_get_const_idx() {
    Object key = arg_take_obj("get_const_idx");
    int i = dict_set(tm->constants, key, NONE_OBJECT);
    return tm_number(i);
}

Object bf_get_const() {
    int num = arg_take_int("get_const");
    int idx = num;
    if (num < 0) {
        idx += DICT_LEN(tm->constants);
    }
    if (idx < 0 || idx >= DICT_LEN(tm->constants)) {
        tm_raise("get_const(idx): out of range [%d]", num);
    }
    return GET_CONST(idx);
}
/* for save */
Object bf_get_const_len() {
    return tm_number(DICT_LEN(tm->constants));
}

Object bf_get_ex_list() {
    return tm->ex_list;
}

Object bf_exists(){
    Object _fname = arg_take_str_obj("exists");
    char* fname = GET_STR(_fname);
    FILE*fp = fopen(fname, "rb");
    if(fp == NULL) return NUMBER_FALSE;
    fclose(fp);
    return NUMBER_TRUE;
}

Object bf_stat(){
    const char *s = arg_take_sz("stat");
    struct stat stbuf;
    if (!stat(s,&stbuf)) { 
        Object st = dict_new();
        dict_set_by_str(st, "st_mtime", tm_number(stbuf.st_mtime));
        dict_set_by_str(st, "st_atime", tm_number(stbuf.st_atime));
        dict_set_by_str(st, "st_ctime", tm_number(stbuf.st_ctime));
        dict_set_by_str(st, "st_size" , tm_number(stbuf.st_size));
        dict_set_by_str(st, "st_mode",  tm_number(stbuf.st_mode));
        dict_set_by_str(st, "st_nlink", tm_number(stbuf.st_nlink));
        dict_set_by_str(st, "st_dev",   tm_number(stbuf.st_dev));
        dict_set_by_str(st, "st_ino",   tm_number(stbuf.st_ino));
        dict_set_by_str(st, "st_uid",   tm_number(stbuf.st_uid));
        dict_set_by_str(st, "st_gid",   tm_number(stbuf.st_gid));
        return st;
    }
    tm_raise("stat(%s), file not exists or accessable.",s);
    return NONE_OBJECT;
}

Object bf_getcwd() {
    const char* sz_func = "getcwd";
    char buf[1025];
    char* r = getcwd(buf, 1024);
    if (r == NULL) {
        char *msg;
        switch (errno) {
            case EINVAL: msg = "The size of argument is 0";break;
            case ERANGE: msg = "The size argument is greater than 0, but is smaller than the length of the pathname +1.";break;
            case EACCES: msg = "Read or search permission was denied for a component of the pathname.";break;
            case ENOMEM: msg = "Insufficient storage space is available.";break;
        }
        tm_raise("%s: error -- %s", sz_func, msg);
    }
    return string_new(buf);
}

Object bf_chdir() {
    const char* sz_func = "chdir";
    char *path = arg_take_sz(sz_func);
    int r = chdir(path);
    if (r != 0) {
        tm_raise("%s: -- fatal error, can not chdir(\"%s\")", sz_func, path);
    } 
    return NONE_OBJECT;
}

Object bf_get_os_name() {
    const char* sz_func = "getosname";
#ifdef _WINDOWS_H
    return sz_to_string("nt");
#else
    return sz_to_string("posix");
#endif
}

Object bf_listdir() {
    Object list = list_new(10);
    Object path = arg_take_str_obj("listdir");
#ifdef _WINDOWS_H
    WIN32_FIND_DATA Find_file_data;
    Object _path = obj_add(path, string_new("\\*.*"));
    HANDLE h_find = FindFirstFile(GET_STR(_path), &Find_file_data);
    if (h_find == INVALID_HANDLE_VALUE) {
        tm_raise("%s is not a directory", path);
    }
    do {
        if (strcmp(Find_file_data.cFileName, "..")==0 || strcmp(Find_file_data.cFileName, ".") == 0) {
            continue;
        }
        if (Find_file_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // do nothing.
        }
        Object file = string_new(Find_file_data.cFileName);
        obj_append(list, file);
    } while (FindNextFile(h_find, &Find_file_data));
    FindClose(h_find);
#else
    tm_raise("listdir not implemented in posix.");
#endif
    return list;
}

Object bf_traceback() {
    traceback();
    return NONE_OBJECT;
}

/**
 * create a object in tm
 */ 
Object bf_newobj() {
    Object obj = dict_new();
    return obj;
}

/**
 * random
 */
Object bf_random() {
    static long seed = 0;
    if (seed == 0) {
        seed = time(NULL);
        srand(seed);
    }
    int n = rand() % 77;
    // printf("%d\n", n);
    double val = (double)((double) n / (double)77);
    return tm_number(val);
}

void builtins_init() {
    reg_builtin_func("load", bf_load);
    reg_builtin_func("save", bf_save);
    reg_builtin_func("file_append", bf_file_append);
    reg_builtin_func("remove", bf_remove);
    reg_builtin_func("write", bf_write);
    reg_builtin_func("load_module", bf_load_module);
    reg_builtin_func("gettype", bf_gettype);
    reg_builtin_func("istype", bf_istype);
    reg_builtin_func("code8", bf_code8);
    reg_builtin_func("code16", bf_code16);
    reg_builtin_func("code32", bf_code32);
    reg_builtin_func("mmatch", bf_mmatch);
    reg_builtin_func("newobj", bf_newobj);

    /* python built-in functions */
    reg_builtin_func("globals", bf_globals);
    reg_builtin_func("len", bf_len);
    reg_builtin_func("exit", bf_exit);
    reg_builtin_func("input", bf_input);
    reg_builtin_func("str", bf_str);
    reg_builtin_func("int", bf_int);
    reg_builtin_func("float", bf_float);
    reg_builtin_func("print", bf_print);
    reg_builtin_func("chr", bf_chr);
    reg_builtin_func("ord", bf_ord);
    reg_builtin_func("raise", bf_raise);
    reg_builtin_func("system", bf_system);
    reg_builtin_func("apply", bf_apply);
    reg_builtin_func("pow", bf_pow);
    reg_builtin_func("range", bf_range);
    reg_builtin_func("enumerate", bf_enumerate);
    reg_builtin_func("random", bf_random);
    
    /* functions which has impact on vm follow camel case */
    reg_builtin_func("get_const_idx", bf_get_const_idx);
    reg_builtin_func("get_const", bf_get_const);
    reg_builtin_func("get_const_len", bf_get_const_len);
    reg_builtin_func("get_ex_list", bf_get_ex_list);
    reg_builtin_func("set_vm_state", bf_set_vm_state);
    reg_builtin_func("inspect_ptr", bf_inspect_ptr);
    reg_builtin_func("get_current_frame", bf_get_current_frame);
    reg_builtin_func("get_vm_info", bf_get_vm_info);
    reg_builtin_func("traceback", bf_traceback);

    reg_builtin_func("clock", bf_clock);
    reg_builtin_func("time0", bf_time0);
    reg_builtin_func("add_obj_method", bf_add_obj_method);
    reg_builtin_func("read_file", bf_read_file);
    reg_builtin_func("iter", bf_iter);
    reg_builtin_func("next", bf_next);
    reg_builtin_func("sleep", bf_sleep);
    
    reg_builtin_func("exists", bf_exists);
    reg_builtin_func("stat", bf_stat);
    reg_builtin_func("getcwd", bf_getcwd);
    reg_builtin_func("chdir", bf_chdir);
    reg_builtin_func("getosname", bf_get_os_name);
    reg_builtin_func("listdir", bf_listdir);
}

