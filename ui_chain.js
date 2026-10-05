/* Version the imported path: QuickJS caches relative imports even though
 * shadow_load_ui_module gives this entrypoint a fresh name on every open. */
import {createUi} from "./ui_core_v0210.mjs";
import {createController,LAYOUT_MOVY,LAYOUT_LIST} from "/data/UserData/schwung/shared/param_pages/page_controller.mjs";
import {decodeInput,applyInput} from "/data/UserData/schwung/shared/param_pages/page_input.mjs";
const has=n=>typeof globalThis[n]==="function";
const core=createUi({
    createController,decodeInput,applyInput,
    getParam:k=>host_module_get_param(k),
    setParam:(k,v)=>host_module_set_param(k,String(v)),
    announce:t=>{if(has("host_send_screenreader"))host_send_screenreader(t);},
    now:()=>Date.now(),
    layout:()=>{
        if(has("tts_get_enabled")&&tts_get_enabled())return LAYOUT_LIST;
        return has("param_view_get_mode")&&param_view_get_mode()===1?LAYOUT_MOVY:LAYOUT_LIST;
    },
    draw:{fillRect:(...a)=>fill_rect(...a),print:(...a)=>print(...a),
        textWidth:t=>has("text_width")?text_width(t):String(t).length*5,
        line:has("draw_line")?draw_line:undefined,
        fillCircle:has("fill_circle")?fill_circle:undefined,
        drawCircle:has("draw_circle")?draw_circle:undefined,
        drawArc:has("draw_arc")?draw_arc:undefined},
    clear:()=>clear_screen(),
    trailingMenus:()=>has("shadow_component_trailing_menus")?shadow_component_trailing_menus():[],
    runAction:a=>has("shadow_component_run_action")?shadow_component_run_action(a):false,
    shiftHeld:()=>has("shadow_get_shift_held")&&shadow_get_shift_held()!==0,
    displayOn:()=>!has("shadow_get_display_mode")||shadow_get_display_mode()===1,
    padBlock:on=>{if(has("host_pad_block"))host_pad_block(on?1:0);},
    sendLed:(n,c)=>has("move_midi_internal_send")?move_midi_internal_send([9,144,n,c]):false,
    padSnapshot:()=>has("shadow_get_pad_led_snapshot")?shadow_get_pad_led_snapshot():null
});
/* Shadow UI falls back to its slots screen on an uncaught draw error. Keep the
 * performance screen up and report the actual fault so a hardware-only
 * renderer/input problem can be fixed without losing the live controls. */
const reported=new Set();
function guarded(phase,fn,fallback) {
    return (...args)=>{
        try { return fn(...args); }
        catch(error) {
            const detail=String(error)+(error&&error.stack?"\n"+error.stack:"");
            const signature=phase+":"+detail;
            if(!reported.has(signature)){
                reported.add(signature);
                /* The device console override can itself throw while logging.
                 * A diagnostic must never turn a handled UI fault into exit. */
                try { console.error("STS Lite UI "+signature); } catch(_) {}
            }
            if(phase==="tick"){
                try {
                    clear_screen();
                    print(2,20,"STS LITE UI ERROR",1);
                    print(2,32,String(error).slice(0,24),1);
                } catch(_) {}
            }
            return fallback;
        }
    };
}
globalThis.chain_ui={
    init:guarded("init",core.init),
    tick:guarded("tick",core.tick),
    onMidiMessageInternal:guarded("midi",core.onMidi),
    handleBack:guarded("back",core.handleBack,false)
};
