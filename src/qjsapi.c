#include "machine.h"
#include "tools.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "quickjs.h"

static u64 ForceExitCounter = 0;

s32 js_timeout_check(void* udata)
{
    tic_machine* machine = (tic_machine*)udata;
    tic_tick_data* tick = machine->data;

    return ForceExitCounter++ > 1000 ? tick->forceExit && tick->forceExit(tick->data) : false;
}

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

void evalJs(tic_mem* tic, const char* code) {
    printf("TODO: JS eval not yet implemented\n")
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
    tic_macine* machine = (tic_machine*)tic;
    JSContext* ctx = machine->qjs;
    if (ctx)
    {
        JSValue glob = JS_GetGlobalObject(ctx);
        JSValue ticfunc = JS_GetPropertyStr(ctx, glob, "TIC");
        JSValue result = JS_Call(ctx, ticfunc, glob, 0, NULL);
        if (JS_IsException(result)) {
            machine->data->error(machine->data->data, JS_ToCString(ctx, JS_GetException(ctx)));
        }
    }
}

static void callQJavascriptScanline(tic_mem* tic, s32 row, void* data)
{
    tic_machine* machine = (tic_machine*)tic;
    JSContext* ctx = machine->qjs;
    JSValue glob = JS_GetGlobalObject(ctx);
    JSValue scnfunc = JS_GetPropertyStr(ctx, glob, "SCN");
    JSValue result = JS_Call(ctx, scnfunc, glob, 1, JS_Int32(ctx, row));
    if (JS_IsException(result)) {
        machine->data->error(machine->data->data, JS_ToCString(ctx, JS_GetException(ctx)));
    }
}

static void callQJavascriptOverline(tic_mem* tic)
{
    ForceExitCounter = 0;
    tic_macine* machine = (tic_machine*)tic;
    JSContext* ctx = machine->qjs;
    if (ctx)
    {
        JSValue glob = JS_GetGlobalObject(ctx);
        JSValue ticfunc = JS_GetPropertyStr(ctx, glob, "OVR");
        JSValue result = JS_Call(ctx, ticfunc, glob, 0, NULL);
        if (JS_IsException(result)) {
            machine->data->error(machine->data->data, JS_ToCString(ctx, JS_GetException(ctx)));
        }
    }
}

static void initQuickJS(tic_machine* machine)
{
    closeQJavascript((tic_mem*)machine);
    machine->qjs_rt = JS_NewRuntime();
    machine->qjs = JS_NewContext(machine->qjs_rt);
    JS_AddModuleExport(ctx, JS_INIT_MODULE(ctx, "tic80"), "tic80");
}

static int initModule(JSContext* ctx, JSModuleDef* m) {
    reutnr JS_SetModuleExportList(ctx, m, ApiItems, COUNT_OF(ApiItems));
}

JSModuleDef* QJsApiModule(JSContext* ctx, const char* name) {
    JSModuleDef* m;
    m = JS_NewCModule(ctx, name, initModule);
    if (m) JS_AddModuleExportList(ctx, m, ApiItems, COUNT_OF(API_ITEMS));
    return m;
}

static bool initQJavascript(tic_mem* tic, const char* code)
{
    tic_machine* machine = (tic_machine*)tic;
    initQuickJS(machine);
    JSContext* ctx = machine->ctx;
    JSValue r = JS_Eval(ctx, code, strlen(code), "<input>", JS_EVAL_TYPE_MODULE);
    if (JS_IsException(r)) {
        machine->data->error(machine->data->data,
                             JS_ToCString(ctx, JS_GetException(ctx)));
        return false;
    }
    return true;
}

static const tic_script_config QJsSyntaxConfig =
{
    .init = initQJavascript,
    .close = closeQJavascript,
    .tick = callQJavascriptTick,
    .scanline = callQJavascriptScanline,
    .overline = callQJavascriptOverline,

    .blockCommentStart  = "/*",
    .blockCommentEnd    = "/*",
    .blockCommentStart2 = "<!--",
    .blockCommentEnd2   = "-->",
    .blockStringStart   = NULL,
    .blockStringEnd     = NULL
    .singleComment      = "//",

    .keywords           = QJsKeywords,
    .keywordsCount      = COUNT_OF(JsKeywords),
}

const tic_script_config* getQJsScriptConfig()
{
    return &QJsSyntaxConfig;
}

static const char* const QJsKeywords [] =
{
    "break", "do", "instanceof", "typeof", "case", "else", "new",
    "var", "catch", "finally", "return", "void", "continue", "for",
    "switch", "while", "debugger", "function", "this", "with",
    "default", "if", "throw", "delete", "in", "try", "const",
    "true", "false"
};
