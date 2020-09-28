#include "machine.h"
#include "tools.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "quickjs.h"

static u64 ForceExitCounter = 0;
//static const char[] TicMachine = "_TIC80"
static JSClassID TicMachineID;
static JSClassDef TicMachine = {
    "_TIC80",
};

s32 qjs_timeout_check(void* udata)
{
    tic_machine* machine = (tic_machine*)udata;
    tic_tick_data* tick = machine->data;

    return ForceExitCounter++ > 1000 ? tick->forceExit && tick->forceExit(tick->data) : false;
}

static tic_machine* getQuickJSMachine(JSContext* ctx)
{
    JSValue glob = JS_GetGlobalObject(ctx);
    JSValue machine_opaque = JS_GetPropertyStr(ctx, glob, "_TIC80");
    return JS_GetOpaque(machine_opaque, TicMachineID);
}

static JSValue qjs_reset(JSContext* ctx, JSValueConst this, int argc, JSValueConst* argv) {
    tic_machine* machine = getQuickJSMachine(ctx);
    machine->state.initialized = false;
    return JS_UNDEFINED;
}

static JSValue qjs_cls(JSContext* ctx, JSValueConst this, int argc, JSValueConst *argv) {
    tic_mem* tic = (tic_mem*)getQuickJSMachine(ctx);
    s32 color = 0;
    if (argc == 1)
        JS_ToInt32(ctx, &color, argv[0]);
    else if (argc > 1)
        return JS_EXCEPTION;
    tic_api_cls(tic, color);
    return JS_UNDEFINED;
}

static JSValue qjs_print(JSContext* ctx, JSValueConst this, int argc, JSValueConst* argv) {
    tic_mem* tic = (tic_mem*) getQuickJSMachine(ctx);
    s32 x = 0, y = 0, color = TIC_PALETTE_SIZE-1, scale = 1;
    bool fixed = false, alt = false;
    if (argc >= 8 || argc <= 0) {
        return JS_EXCEPTION;
    }
    if (argc >= 2) {
        if (JS_ToInt32(ctx, &x, argv[1]))
            return JS_EXCEPTION;
    }
    if (argc >= 3) {
        if (JS_ToInt32(ctx, &y, argv[2]))
            return JS_EXCEPTION;
    }
    if (argc >= 4) {
        if (JS_ToInt32(ctx, &color, argv[3]))
            return JS_EXCEPTION;
    }
    if (argc >= 5) {
        fixed = JS_ToBool(ctx, argv[4]);
    }
    if (argc >= 6) {
        if (JS_ToInt32(ctx, &scale, argv[5]))
            return JS_EXCEPTION;
    }
    if (argc == 7) {
        alt = JS_ToBool(ctx, argv[6]);
    }
    const char* txt = JS_ToCString(ctx, argv[0]);
    return JS_NewInt32(ctx, tic_api_print(tic, txt ? txt : "nil", x, y, color, fixed, scale, alt));
}

static const JSCFunctionListEntry ApiItems[] = {
    JS_CFUNC_DEF("print", 7, qjs_print),
    JS_CFUNC_DEF("reset", 0, qjs_reset),
    JS_CFUNC_DEF("cls",   1, qjs_cls),
};

static inline bool isalnum_(char c) {return isalnum(c) || c == '_';}
static const tic_outline_item* getJsOutline(const char* code, s32* size)
{
    enum{Size = sizeof(tic_outline_item)};
    *size = 0;
    static tic_outline_item* items = NULL;

    if(items)
    {
        free(items);
        items = NULL;
    }

    const char* ptr = code;

    while(true)
    {
        static const char FuncString[] = "function ";

        ptr = strstr(ptr, FuncString);

        if(ptr)
        {
            ptr += sizeof FuncString - 1;

            const char* start = ptr;
            const char* end = start;

            while(*ptr)
            {
                char c = *ptr;

                if(isalnum_(c));
                else if(c == '(')
                {
                    end = ptr;
                    break;
                }
                else break;

                ptr++;
            }

            if(end > start)
            {
                items = realloc(items, (*size + 1) * Size);

                items[*size].pos = start;
                items[*size].size = end - start;

                (*size)++;
            }
        }
        else break;
    }

    return items;
}

void evalQJs(tic_mem* tic, const char* code) {
    printf("TODO: JS eval not yet implemented\n");
}

static void closeQJavascript(tic_mem* tic)
{
    tic_machine* machine = (tic_machine*)tic;
    if(machine->qjs_rt) JS_RunGC(machine->qjs_rt);
    if(machine->qjs)
    {
        free(machine->qjs);
        machine->qjs = NULL;
    }
    if(machine->qjs_rt)
    {
        free(machine->qjs_rt);
        machine->qjs_rt = NULL;
    }
}

static void callQJavascriptTick(tic_mem* tic)
{
    ForceExitCounter = 0;
    tic_machine* machine = (tic_machine*)tic;
    JSContext* ctx = machine->qjs;
    if (ctx)
    {
        JSValue glob = JS_GetGlobalObject(ctx);
        JSValue ticfunc = JS_GetPropertyStr(ctx, glob, "TIC");
        JSValue result = JS_Call(ctx, ticfunc, glob, 0, NULL);
        if (JS_IsException(result)) {
            const char* str = JS_ToCString(ctx, JS_GetException(ctx));
            machine->data->error(machine->data->data, str);
            JS_FreeCString(ctx, str);
        }
        JS_FreeValue(ctx, result);
        JS_FreeValue(ctx, ticfunc);
        JS_FreeValue(ctx, glob);
    }
}

static void callQJavascriptScanline(tic_mem* tic, s32 row, void* data)
{
    tic_machine* machine = (tic_machine*)tic;
    JSContext* ctx = machine->qjs;
    JSValue glob = JS_GetGlobalObject(ctx);
    JSValue scnfunc = JS_GetPropertyStr(ctx, glob, "SCN");
    JSValue args = JS_NewInt32(ctx, row);
    JSValue result = JS_Call(ctx, scnfunc, glob, 1, &args);
    if (JS_IsException(result)) {
        machine->data->error(machine->data->data, JS_ToCString(ctx, JS_GetException(ctx)));
    }
    JS_FreeValue(ctx, glob);
    JS_FreeValue(ctx, scnfunc);
    JS_FreeValue(ctx, args);
    JS_FreeValue(ctx, result);
}

static void callQJavascriptOverline(tic_mem* tic, void* data)
{
    ForceExitCounter = 0;
    tic_machine* machine = (tic_machine*)tic;
    JSContext* ctx = machine->qjs;
    if (ctx)
    {
        JSValue glob = JS_GetGlobalObject(ctx);
        JSValue ticfunc = JS_GetPropertyStr(ctx, glob, "OVR");
        JSValue result = JS_Call(ctx, ticfunc, glob, 0, NULL);
        if (JS_IsException(result)) {
            machine->data->error(machine->data->data, JS_ToCString(ctx, JS_GetException(ctx)));
        }
        JS_FreeValue(ctx, glob);
        JS_FreeValue(ctx, ticfunc);
        JS_FreeValue(ctx, result);
    }
}

static int initModule(JSContext* ctx, JSModuleDef* m) {
    return JS_SetModuleExportList(ctx, m, ApiItems, COUNT_OF(ApiItems));
}

static JSModuleDef* QJsApiModule(JSContext* ctx, const char* name) {
    JSModuleDef* m;
    m = JS_NewCModule(ctx, name, initModule);
    if (m) JS_AddModuleExportList(ctx, m, ApiItems, COUNT_OF(ApiItems));
    return m;
}

static void initQuickJS(tic_machine* machine)
{
    closeQJavascript((tic_mem*)machine);
    machine->qjs_rt = JS_NewRuntime();
    machine->qjs = JS_NewContext(machine->qjs_rt);
    JS_AddModuleExport(machine->qjs, QJsApiModule(machine->qjs, "tic80"), "tic80");
}

static bool initQJavascript(tic_mem* tic, const char* code)
{
    tic_machine* machine = (tic_machine*)tic;
    initQuickJS(machine);
    JSContext* ctx = machine->qjs;
    JS_NewClassID(&TicMachineID);
    JSValue tic_js = JS_NewObjectClass(ctx, TicMachineID);
    JS_SetOpaque(tic_js, machine);
    JS_SetPropertyStr(ctx, JS_GetGlobalObject(ctx), "_TIC80", tic_js);
    JSValue r = JS_Eval(ctx, code, strlen(code), "<input>", JS_EVAL_TYPE_MODULE);
    if (JS_IsException(r)) {
        machine->data->error(machine->data->data,
                             JS_ToCString(ctx, JS_GetException(ctx)));
        return false;
    }
    JS_FreeValue(ctx, r);
    return true;
}

static const char* const QJsKeywords [] =
{
    "break", "do", "instanceof", "typeof", "case", "else", "new",
    "var", "catch", "finally", "return", "void", "continue", "for",
    "switch", "while", "debugger", "function", "this", "with",
    "default", "if", "throw", "delete", "in", "try", "const",
    "true", "false", "let", "async", "await", "static", "export",
    "extends", "import", "as", "from"
};

static const tic_script_config QJsSyntaxConfig =
{
    .init = initQJavascript,
    .close = closeQJavascript,
    .tick = callQJavascriptTick,
    .scanline = callQJavascriptScanline,
    .overline = callQJavascriptOverline,

    .getOutline = getJsOutline,
    .eval = evalQJs,

    .blockCommentStart  = "/*",
    .blockCommentEnd    = "/*",
    .blockCommentStart2 = "<!--",
    .blockCommentEnd2   = "-->",
    .blockStringStart   = NULL,
    .blockStringEnd     = NULL,
    .singleComment      = "//",

    .keywords           = QJsKeywords,
    .keywordsCount      = COUNT_OF(QJsKeywords),
};

const tic_script_config* getQJsScriptConfig()
{
    return &QJsSyntaxConfig;
}
