#include "config_portal.h"

#include <inttypes.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "portal_auth.h"
#include "telegram_notifier.h"

#define REQUEST_BODY_MAX 3072U
#define CALIBRATION_DELAY_MIN_SECONDS 5
#define CALIBRATION_DELAY_MAX_SECONDS 120
#define SESSION_COOKIE_NAME "motion_session"
#define SESSION_COOKIE_MAX_LENGTH 192U
#define WIFI_SCAN_MAX_RESULTS 20U
#define WIFI_SCAN_TIMEOUT_MS 20000LL
#define WIFI_SCAN_DETECTOR_GUARD_MS 1000LL

static const char *TAG = "config_portal";
static app_runtime_config_t current_config;
static config_portal_diagnostics_t current_diagnostics;
static SemaphoreHandle_t diagnostics_mutex;
static bool current_provisioning_mode;
static atomic_int_fast64_t calibration_deadline_ms;
static SemaphoreHandle_t wifi_scan_mutex;
static esp_event_handler_instance_t wifi_scan_event_instance;
static atomic_int_fast64_t wifi_scan_detector_guard_until_ms;

typedef enum {
    WIFI_SCAN_IDLE = 0,
    WIFI_SCAN_RUNNING,
    WIFI_SCAN_READY,
    WIFI_SCAN_ERROR,
} wifi_scan_state_t;

typedef struct {
    char ssid[APP_CONFIG_WIFI_SSID_MAX_LENGTH + 1U];
    int rssi;
    uint8_t channel;
    wifi_auth_mode_t authmode;
} wifi_scan_result_t;

static wifi_scan_state_t wifi_scan_state;
static int64_t wifi_scan_started_ms;
static int64_t wifi_scan_updated_ms;
static esp_err_t wifi_scan_last_error;
static size_t wifi_scan_result_count;
static wifi_scan_result_t wifi_scan_results[WIFI_SCAN_MAX_RESULTS];
static wifi_ap_record_t wifi_scan_raw_results[WIFI_SCAN_MAX_RESULTS];

static const char INDEX_HTML[] =
    "<!doctype html><html lang=en><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>WiFi Motion RSSI</title><style>"
    "*{box-sizing:border-box}html{-webkit-text-size-adjust:100%;overflow-x:hidden}body{font:16px system-ui;max-width:760px;margin:auto;padding:clamp(12px,4vw,20px);background:#f5f7fa;color:#18212b;overflow-wrap:anywhere}"
    "h1{font-size:clamp(1.65rem,7vw,2rem);line-height:1.15;margin:0}h2{line-height:1.2}fieldset{min-width:0;border:1px solid #ccd4dd;border-radius:8px;margin:12px 0;padding:14px;background:white}"
    "label{display:grid;grid-template-columns:minmax(0,1fr) minmax(0,1fr);align-items:center;gap:10px;margin:8px 0}"
    "input,select,button{max-width:100%;min-height:44px;font:inherit;padding:9px 10px}input:not([type=checkbox]),select{width:100%}input[type=checkbox]{width:22px;height:22px;margin:0;accent-color:#1769aa}button{margin:5px;border:1px solid #7b8794;border-radius:6px;touch-action:manipulation;white-space:normal}button:focus-visible,input:focus-visible,select:focus-visible,a:focus-visible{outline:3px solid #60a5fa;outline-offset:2px}pre{max-width:100%;overflow:auto;white-space:pre-wrap;background:#111;color:#9f9;padding:12px}"
    ".motion-card{display:flex;align-items:center;gap:16px;border-radius:12px;padding:18px;margin:14px 0;color:#fff;background:#6b7280;box-shadow:0 3px 10px #0002}"
    ".motion-dot{width:28px;height:28px;border-radius:50%;background:currentColor;border:4px solid #fff;flex:none}"
    ".motion-label{display:block;font-size:1.35rem}.motion-detail{margin-top:3px;font-size:.9rem}"
    ".motion-card.idle{background:#147d3f}.motion-card.motion{background:#c62828}.motion-card.calibrating{background:#a15c00}.motion-card.error{background:#4b5563}"
    ".motion-card.motion .motion-dot{animation:pulse .7s ease-in-out infinite alternate}@keyframes pulse{to{transform:scale(1.3);opacity:.65}}"
    "@media(prefers-reduced-motion:reduce){.motion-card.motion .motion-dot{animation:none}}"
    ".chart-panel{border:1px solid #ccd4dd;border-radius:8px;margin:12px 0;padding:14px;background:#fff}"
    ".chart-panel h2{margin:0 0 8px}.chart-wrap{position:relative;width:100%;height:clamp(220px,62vw,270px)}"
    "#motion_chart{width:100%;height:100%;display:block}.chart-controls{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:6px 12px;font-size:.9rem;margin-bottom:8px}"
    ".chart-option{min-width:0;display:flex;align-items:center;gap:4px}.chart-controls label{min-width:0;display:flex;align-items:center;gap:7px;margin:0;cursor:pointer;flex:1}.chart-controls input{padding:0;flex:none}.swatch{width:20px;height:4px;background:#1769aa;flex:none}.swatch.threshold{background:#d97706}.swatch.csi{background:#7c3aed}.swatch.csi-threshold{background:#db2777}.swatch.hit{background:#c62828}.swatch.csi-hit{background:#7c3aed}"
    ".help{width:32px;min-width:32px;height:32px;min-height:32px;border:1px solid #7b8794;border-radius:50%;padding:0;margin:0;background:#fff;color:#334155;font-size:.85rem;font-weight:700;cursor:help}.help:focus{outline:3px solid #93c5fd;outline-offset:1px}"
    ".chart-help,.chart-summary,.calibration-status{margin:8px 0 0;color:#4b5563;font-size:.9rem}.calibration-panel{border:1px solid #ccd4dd;border-radius:8px;margin:12px 0;padding:14px;background:#fff}.calibration-panel h2{margin:0 0 6px}.calibration-actions{display:flex;align-items:center;flex-wrap:wrap;gap:8px}.calibration-actions label{display:inline-flex;grid-template-columns:none;align-items:center;gap:8px;margin:0}"
    ".config-option{display:grid;grid-template-columns:1fr 1fr 24px;align-items:center;gap:8px;margin:8px 0}.config-option label{display:contents}.config-help{margin:4px 0;color:#4b5563;font-size:.9rem}.save-bar{position:sticky;bottom:0;z-index:2;display:flex;align-items:center;justify-content:space-between;gap:12px;padding:10px 12px;margin:0 0 12px;background:#eaf2ff;border:1px solid #8eb8ef;border-radius:8px;box-shadow:0 -3px 10px #0002}.save-bar button{margin:0;background:#1769aa;color:#fff;border:0;border-radius:6px;font-weight:700}.save-bar button:disabled{background:#7b8794}.save-status{font-size:.9rem;color:#334155}"
    ".auth-panel{border:1px solid #8eb8ef;border-radius:10px;padding:18px;background:#fff}.auth-panel h2{margin-top:0}.auth-panel button{background:#1769aa;color:#fff;border:0;border-radius:6px;font-weight:700}.security-bar{display:flex;align-items:center;justify-content:space-between;gap:10px;padding:10px 12px;background:#fff4ce;border:1px solid #d69e00;border-radius:8px}.security-bar p{margin:0;flex:1}.password-panel{background:#fff}"
    ".topbar{display:flex;align-items:center;justify-content:space-between;gap:12px;margin:12px 0 18px}.language-switch{display:flex;gap:4px;padding:3px;border:1px solid #aab5c0;border-radius:8px;background:#fff}.language-switch button{min-height:38px;margin:0;padding:6px 9px;border:0;background:transparent}.language-switch button[aria-pressed=true]{background:#1769aa;color:#fff;font-weight:700}"
    ".wifi-scan-actions{display:flex;align-items:center;gap:8px;flex-wrap:wrap}.wifi-status{color:#4b5563;margin:6px 0}.wifi-networks{list-style:none;margin:8px 0;padding:0;display:grid;gap:6px}.wifi-networks button{width:100%;display:flex;align-items:center;justify-content:space-between;gap:12px;text-align:left;margin:0;border:1px solid #aab5c0;border-radius:6px;background:#f8fafc;color:#18212b}.wifi-networks button[aria-pressed=true]{border:2px solid #1769aa;background:#eaf2ff}.wifi-networks button:disabled{color:#68737f;background:#eceff2}.wifi-network-meta{white-space:nowrap;font-size:.9rem}"
    "@media(max-width:700px){label{grid-template-columns:1fr;gap:4px}.config-option{grid-template-columns:minmax(0,1fr) 44px;gap:6px 8px}.config-option label{display:block;margin:0}.config-option select{grid-column:1/-1}.help{width:44px;min-width:44px;height:44px;min-height:44px}.security-bar{align-items:stretch;flex-direction:column}.security-bar button{width:100%;margin:0}.calibration-actions{align-items:stretch;flex-direction:column}.calibration-actions label{display:grid;grid-template-columns:1fr;align-items:stretch;width:100%;gap:4px}.calibration-actions button,.wifi-scan-actions button{width:100%;margin:0}.wifi-scan-actions{align-items:stretch;flex-direction:column}.wifi-networks button{align-items:flex-start;flex-direction:column;gap:3px}.wifi-network-meta{white-space:normal}.auth-panel button,.password-panel button,#telegram_form button{width:100%;margin:5px 0}.save-bar{position:static;align-items:stretch;flex-direction:column}.save-bar button{width:100%}.chart-controls{grid-template-columns:1fr}.topbar{align-items:flex-start;flex-direction:column}.language-switch{width:100%}.language-switch button{flex:1}}"
    "@media(max-width:380px){body{padding:10px}.auth-panel,fieldset,.chart-panel,.calibration-panel{padding:11px}.motion-card{gap:11px;padding:14px}.motion-dot{width:24px;height:24px}.motion-label{font-size:1.15rem}}"
    "</style><header class=topbar><h1>WiFi Motion RSSI</h1><nav class=language-switch aria-label='Language / Idioma'><button id=lang_en type=button lang=en aria-pressed=true>English</button><button id=lang_es type=button lang=es aria-pressed=false>Español</button></nav></header>"
    "<section id=login_panel class=auth-panel aria-labelledby=login_title><h2 id=login_title data-i18n=admin_access>Administrator access</h2><p data-i18n=login_intro>Sign in to view or change the device.</p>"
    "<form id=login_form><label data-i18n=username>Username<input id=login_username value=admin readonly autocomplete=username></label><label data-i18n=password>Password<input id=login_password type=password required maxlength=64 autocomplete=current-password></label><button type=submit data-i18n=sign_in>Sign in</button></form><p id=login_status role=status aria-live=polite></p></section>"
    "<main id=app hidden><div class=security-bar><p id=security_notice role=status></p><button id=logout type=button data-i18n=sign_out>Sign out</button></div><p id=status data-i18n=loading>Loading...</p>"
    "<section id=motion_card class='motion-card calibrating' role=status aria-live=polite aria-atomic=true>"
    "<span class=motion-dot aria-hidden=true></span><div><strong id=motion_label class=motion-label data-i18n=calibrating_detector>Calibrating detector</strong>"
    "<div id=motion_detail class=motion-detail data-i18n=waiting_data>Waiting for data...</div></div></section>"
    "<section class=chart-panel aria-labelledby=chart_title><h2 id=chart_title data-i18n=realtime_activity>Real-time activity</h2>"
    "<div class=chart-controls data-i18n-aria=visible_chart_elements aria-label='Visible chart elements'>"
    "<span class=chart-option><label data-i18n=score_rssi><input id=show_rssi_score type=checkbox checked><i class=swatch aria-hidden=true></i>RSSI score</label><button class=help type=button data-help-key=help_rssi_score data-aria-key=help_rssi_score_label title='About RSSI score' aria-label='About RSSI score'>?</button></span>"
    "<span class=chart-option><label data-i18n=threshold_rssi><input id=show_rssi_threshold type=checkbox checked><i class='swatch threshold' aria-hidden=true></i>RSSI threshold</label><button class=help type=button data-help-key=help_rssi_threshold data-aria-key=help_rssi_threshold_label title='About RSSI threshold' aria-label='About RSSI threshold'>?</button></span>"
    "<span class=chart-option><label data-i18n=score_csi><input id=show_csi_score type=checkbox checked><i class='swatch csi' aria-hidden=true></i>CSI score</label><button class=help type=button data-help-key=help_csi_score data-aria-key=help_csi_score_label title='About CSI score' aria-label='About CSI score'>?</button></span>"
    "<span class=chart-option><label data-i18n=threshold_csi><input id=show_csi_threshold type=checkbox checked><i class='swatch csi-threshold' aria-hidden=true></i>CSI threshold</label><button class=help type=button data-help-key=help_csi_threshold data-aria-key=help_csi_threshold_label title='About CSI threshold' aria-label='About CSI threshold'>?</button></span>"
    "<span class=chart-option><label data-i18n=detection_rssi><input id=show_rssi_hits type=checkbox checked><i class='swatch hit' aria-hidden=true></i>RSSI detection</label><button class=help type=button data-help-key=help_rssi_hits data-aria-key=help_rssi_hits_label title='About RSSI detections' aria-label='About RSSI detections'>?</button></span>"
    "<span class=chart-option><label data-i18n=detection_csi><input id=show_csi_hits type=checkbox checked><i class='swatch csi-hit' aria-hidden=true></i>CSI detection</label><button class=help type=button data-help-key=help_csi_hits data-aria-key=help_csi_hits_label title='About CSI detections' aria-label='About CSI detections'>?</button></span></div>"
    "<p id=chart_help class=chart-help role=status aria-live=polite data-i18n=chart_help_intro>Tap a ? button to learn what each element means.</p>"
    "<div class=chart-wrap><canvas id=motion_chart role=img data-i18n-aria=chart_aria aria-label='Time chart of scores, thresholds and detection markers'></canvas></div>"
    "<p id=chart_summary class=chart-summary data-i18n=chart_waiting>Waiting for samples to draw the chart.</p></section>"
    "<section class=calibration-panel aria-labelledby=calibration_title><h2 id=calibration_title data-i18n=manual_recalibration>Manual recalibration</h2>"
    "<p data-i18n=calibration_intro>Set a delay, leave the room and keep it empty while both detectors calibrate.</p>"
    "<div class=calibration-actions><label data-i18n=start_in>Start in <select id=calibration_delay><option value=10>10 s</option><option value=20 selected>20 s</option><option value=30>30 s</option><option value=60>60 s</option></select></label>"
    "<button id=start_calibration type=button data-i18n=schedule_recalibration>Schedule recalibration</button></div>"
    "<p id=calibration_status class=calibration-status role=status aria-live=polite data-i18n=no_recalibration>No recalibration scheduled.</p></section>"
    "<form id=f><fieldset><legend>Wi-Fi</legend>"
    "<div class=wifi-scan-actions><button id=scan_wifi type=button data-i18n=scan_networks>Scan nearby networks</button><span id=wifi_scan_status class=wifi-status role=status aria-live=polite data-i18n=scan_not_started>Scan not started yet.</span></div>"
    "<ul id=wifi_networks class=wifi-networks data-i18n-aria=wifi_networks_aria aria-label='Wi-Fi networks found'></ul>"
    "<label data-i18n=selected_ssid>Selected or manual SSID<input id=ssid maxlength=32 autocomplete=off></label>"
    "<label data-i18n=network_password>Network password<input id=password type=password maxlength=64 autocomplete=new-password data-i18n-placeholder=keep_password_placeholder placeholder='leave blank to keep the current password'></label>"
    "<label data-i18n=open_or_clear>Open network or clear saved password<input id=clear_password type=checkbox></label>"
    "<p id=wifi_password_status class=wifi-status data-i18n=wifi_password_intro>The saved password is kept when the SSID is unchanged and this field is blank.</p>"
    "</fieldset><fieldset><legend data-i18n=rssi_detector>RSSI detector</legend>"
    "<p data-i18n=save_notice>Changes in this section are applied only after saving and restarting.</p>"
    "<div class=config-option><label for=detection_source data-i18n=detection_source>Detection source</label><select id=detection_source><option value=rssi data-i18n=rssi_only>RSSI only</option><option value=csi data-i18n=csi_only>CSI only</option><option value=both data-i18n=rssi_or_csi>RSSI or CSI</option></select><button class=help type=button data-target=config_help data-help-key=help_source data-aria-key=help_source_label aria-label='About Detection source'>?</button></div>"
    "<div class=config-option><label for=profile data-i18n=profile>Profile</label><select id=profile><option value=low data-i18n=low_sensitivity>Low sensitivity</option><option value=balanced data-i18n=balanced>Balanced</option><option value=high data-i18n=high_sensitivity>High sensitivity</option></select><button class=help type=button data-target=config_help data-help-key=help_profile data-aria-key=help_profile_label aria-label='About Profile'>?</button></div>"
    "<div class=config-option><label for=algorithm data-i18n=algorithm>Algorithm</label><select id=algorithm><option value=mean_absolute_difference data-i18n=mean_difference>Mean difference</option><option value=standard_deviation data-i18n=standard_deviation>Standard deviation</option><option value=sample_variance data-i18n=variance>Variance</option><option value=range data-i18n=range>Maximum-minimum range</option><option value=median_absolute_deviation data-i18n=median_deviation>Median deviation</option></select><button class=help type=button data-target=config_help data-help-key=help_algorithm data-aria-key=help_algorithm_label aria-label='About Algorithm'>?</button></div>"
    "<div class=config-option><label for=baseline_mode data-i18n=baseline>Baseline</label><select id=baseline_mode><option value=mean_stddev data-i18n=mean_stddev>Mean and deviation</option><option value=median_mad data-i18n=robust_median>Robust median</option></select><button class=help type=button data-target=config_help data-help-key=help_baseline data-aria-key=help_baseline_label aria-label='About Baseline'>?</button></div>"
    "<p id=config_help class=config-help role=status aria-live=polite data-i18n=config_help_intro>Tap a ? button to learn about these options.</p>"
    "<label data-i18n=interval>Interval (ms)<input id=interval_ms type=number min=50 max=5000></label>"
    "<label data-i18n=window>Window<input id=window_size type=number min=4 max=128></label>"
    "<label data-i18n=calibration>Calibration<input id=calibration_samples type=number min=10 max=512></label>"
    "<label data-i18n=sigma_multiplier>Sigma multiplier<input id=sigma_multiplier type=number min=1 max=12 step=.01></label>"
    "<label data-i18n=minimum_threshold>Minimum threshold<input id=minimum_threshold type=number min=.01 max=10 step=.01></label>"
    "<label data-i18n=release_ratio>Release ratio<input id=release_threshold_ratio type=number min=.01 max=.99 step=.01></label>"
    "<label data-i18n=baseline_adaptation>Baseline adaptation<input id=baseline_update_alpha type=number min=.0001 max=1 step=.0001></label>"
    "<label data-i18n=trigger_confirmation>Trigger confirmation<input id=trigger_consecutive type=number min=1 max=100></label>"
    "<label data-i18n=release_confirmation>Release confirmation<input id=clear_consecutive type=number min=1 max=100></label>"
    "</fieldset><div class=save-bar><span id=config_save_status class=save-status role=status aria-live=polite data-i18n=no_pending_changes>No pending changes.</span><button id=save_config type=submit data-i18n=save_restart>Save Wi-Fi and detector; restart</button></div></form>"
    "<fieldset><legend data-i18n=import_export>Import / export</legend><input id=file type=file accept=application/json>"
    "<button id=import type=button data-i18n=import>Import</button><a href=/api/export><button type=button data-i18n=export>Export</button></a>"
    "<button id=reset type=button data-i18n=factory_reset>Factory reset</button></fieldset>"
    "<fieldset><legend data-i18n=telegram_notifications>Telegram notifications</legend><p data-i18n=telegram_intro>Sends each detection through an independent queue. The token is never displayed again.</p><form id=telegram_form>"
    "<label data-i18n=enable_notifications>Enable notifications<input id=telegram_enabled type=checkbox></label>"
    "<label data-i18n=bot_token>Bot token<input id=telegram_token type=password maxlength=128 autocomplete=new-password data-i18n-placeholder=keep_token_placeholder placeholder='leave blank to keep the saved token'></label>"
    "<label data-i18n=clear_token>Clear saved token<input id=telegram_clear_token type=checkbox></label>"
    "<label>Chat ID<input id=telegram_chat_id inputmode=numeric maxlength=32 autocomplete=off></label>"
    "<button id=save_telegram type=submit data-i18n=save_telegram>Save Telegram settings</button><button id=test_telegram type=button data-i18n=test_telegram>Send test message</button></form><p id=telegram_status class=wifi-status role=status aria-live=polite data-i18n=telegram_not_loaded>Telegram not loaded yet.</p></fieldset>"
    "<fieldset class=password-panel><legend data-i18n=admin_password>Administrator password</legend><form id=password_form><label data-i18n=current_password>Current password<input id=current_admin_password type=password required maxlength=64 autocomplete=current-password></label><label data-i18n=new_password>New password<input id=new_admin_password type=password required minlength=5 maxlength=64 autocomplete=new-password></label><button type=submit data-i18n=change_password>Change password</button></form><p id=password_status role=status aria-live=polite></p></fieldset>"
    "<h2 data-i18n=diagnostics>Diagnostics</h2><pre id=diag></pre></main><script>"
    "const T={en:{admin_access:'Administrator access',login_intro:'Sign in to view or change the device.',username:'Username',password:'Password',sign_in:'Sign in',sign_out:'Sign out',loading:'Loading...',calibrating_detector:'Calibrating detector',waiting_data:'Waiting for data...',realtime_activity:'Real-time activity',visible_chart_elements:'Visible chart elements',score_rssi:'RSSI score',threshold_rssi:'RSSI threshold',score_csi:'CSI score',threshold_csi:'CSI threshold',detection_rssi:'RSSI detection',detection_csi:'CSI detection',chart_help_intro:'Tap a ? button to learn what each element means.',chart_aria:'Time chart of scores, thresholds and detection markers',chart_waiting:'Waiting for samples to draw the chart.',manual_recalibration:'Manual recalibration',calibration_intro:'Set a delay, leave the room and keep it empty while both detectors calibrate.',start_in:'Start in',schedule_recalibration:'Schedule recalibration',no_recalibration:'No recalibration scheduled.',scan_networks:'Scan nearby networks',scan_not_started:'Scan not started yet.',wifi_networks_aria:'Wi-Fi networks found',selected_ssid:'Selected or manual SSID',network_password:'Network password',keep_password_placeholder:'leave blank to keep the current password',open_or_clear:'Open network or clear saved password',wifi_password_intro:'The saved password is kept when the SSID is unchanged and this field is blank.',rssi_detector:'RSSI detector',save_notice:'Changes in this section are applied only after saving and restarting.',detection_source:'Detection source',rssi_only:'RSSI only',csi_only:'CSI only',rssi_or_csi:'RSSI or CSI',profile:'Profile',low_sensitivity:'Low sensitivity',balanced:'Balanced',high_sensitivity:'High sensitivity',algorithm:'Algorithm',mean_difference:'Mean difference',standard_deviation:'Standard deviation',variance:'Variance',range:'Maximum-minimum range',median_deviation:'Median deviation',baseline:'Baseline',mean_stddev:'Mean and deviation',robust_median:'Robust median',config_help_intro:'Tap a ? button to learn about these options.',interval:'Interval (ms)',window:'Window',calibration:'Calibration',sigma_multiplier:'Sigma multiplier',minimum_threshold:'Minimum threshold',release_ratio:'Release ratio',baseline_adaptation:'Baseline adaptation',trigger_confirmation:'Trigger confirmation',release_confirmation:'Release confirmation',no_pending_changes:'No pending changes.',save_restart:'Save Wi-Fi and detector; restart',import_export:'Import / export',import:'Import',export:'Export',factory_reset:'Factory reset',telegram_notifications:'Telegram notifications',telegram_intro:'Sends each detection through an independent queue. The token is never displayed again.',enable_notifications:'Enable notifications',bot_token:'Bot token',keep_token_placeholder:'leave blank to keep the saved token',clear_token:'Clear saved token',save_telegram:'Save Telegram settings',test_telegram:'Send test message',telegram_not_loaded:'Telegram not loaded yet.',admin_password:'Administrator password',current_password:'Current password',new_password:'New password',change_password:'Change password',diagnostics:'Diagnostics',help_rssi_score_label:'About RSSI score',help_rssi_threshold_label:'About RSSI threshold',help_csi_score_label:'About CSI score',help_csi_threshold_label:'About CSI threshold',help_rssi_hits_label:'About RSSI detections',help_csi_hits_label:'About CSI detections',help_source_label:'About Detection source',help_profile_label:'About Profile',help_algorithm_label:'About Algorithm',help_baseline_label:'About Baseline',help_rssi_score:'Recent variation in received Wi-Fi power. A higher value means the RSSI detector sees more change.',help_rssi_threshold:'Level the RSSI score must exceed for several samples before motion is declared.',help_csi_score:'Detailed Wi-Fi channel variation calculated with CSI. It can control the main state when CSI only or RSSI or CSI is selected.',help_csi_threshold:'Adaptive limit of the CSI detector. One isolated rise is not enough; consecutive samples are also required.',help_rssi_hits:'Numbered red line marking each time the RSSI detector enters the motion state.',help_csi_hits:'Purple C# line marking each CSI detection. It controls the main card and LED when CSI is part of the selected source.',help_source:'Choose which detector controls the main card, LED and notifications. RSSI or CSI declares motion when either detector triggers.',help_profile:'The profile adjusts sensitivity and confirmation times together. Low reduces false alarms, High reacts sooner, and Balanced is the recommended starting point.',help_algorithm:'Defines how recent sample variation is summarized. Changing it alters the RSSI score; CSI uses its own configuration.',help_baseline:'The normal level of a quiet room. Mean and deviation suits regular data; Robust median tolerates isolated spikes better.',default_password_warning:'Warning: you are still using the initial admin password. Change it below.',admin_session:'Admin session active. This page uses local HTTP; do not expose it to the Internet.',session_expired:'The session is invalid or has expired.',auth_required:'Authentication required',checking:'Checking...',session_closed:'Signed out.',saving:'Saving...',wifi_password_set:'A password is saved. It will be kept unless you change SSID or select the clear option.',wifi_no_password:'The current network has no saved password.',recovery_mode:'Recovery AP mode',local_connected:'Connected to the local network',new_network_password:'Enter the new network password or mark it as an open network.',wifi_will_clear:'The saved Wi-Fi password will be removed when you save.',wifi_only_if_new:'The password changes only when you enter a new one.',current_protected:'Current protected network: leaving the field blank keeps its password.',protected_password:'Protected network: enter its password before saving.',open_network:'Open network: it will be saved without a password.',current_selected:'Current network selected; no pending changes.',new_selected:'New network selected; save and restart are still required.',current_suffix:' (current)',channel:'channel',unsupported:'not supported',no_networks:'No visible networks were found.',scanning:'Scanning networks; detection is paused to avoid false alarms...',networks_found:'Visible networks: {n}.',scan_error:'Scan error: {error}',starting_scan:'Starting scan...',token_saved:'Token saved. ',no_token:'No token saved. ',automatic_on:'Automatic notifications enabled for {source}. ',automatic_off:'Automatic notifications disabled. ',health:'Sent: {sent} · failures: {failures} · dropped: {dropped} · queue: {queue}',last_http:' · last HTTP: {status}',saving_telegram:'Saving Telegram...',test_sent:'Test message sent. Total sent: {n}',test_failed:'Telegram rejected or could not send the message. HTTP {status} · {error}',test_queued:'Message queued; waiting for Telegram...',pending_changes:'Pending changes: not applied yet.',saving_restart:'Saving and preparing to restart...',restarting:'Restarting...',saved_reconnect:'Configuration saved. Reconnecting in 7 seconds...',save_failed:'Could not save: {error}',factory_confirm:'Delete configuration and Wi-Fi credentials?',recalibration_scheduled:'Recalibration scheduled. Leave the room.',motion_detected:'MOTION DETECTED',no_motion:'No motion',read_error:'Read error',unknown_state:'Unknown state',calibration_countdown:'Recalibration starts in {seconds} s. Leave the room.',calibrating_now:'Calibrating now. Keep the room empty.',calibration_done:'Calibration complete; detectors ready.',chart_summary:'Visible series: {shown} · visible RSSI detections: {rssi} · CSI: {csi}',hidden:'hidden',no_connection:'No connection',device_unreachable:'Could not contact the device.',rssi_or_csi_source:'RSSI or CSI'},"
    "es:{admin_access:'Acceso de administrador',login_intro:'Inicia sesión para consultar o cambiar el dispositivo.',username:'Usuario',password:'Contraseña',sign_in:'Entrar',sign_out:'Cerrar sesión',loading:'Cargando...',calibrating_detector:'Calibrando detector',waiting_data:'Esperando datos...',realtime_activity:'Actividad en tiempo real',visible_chart_elements:'Elementos visibles de la gráfica',score_rssi:'Score RSSI',threshold_rssi:'Umbral RSSI',score_csi:'Score CSI',threshold_csi:'Umbral CSI',detection_rssi:'Detección RSSI',detection_csi:'Detección CSI',chart_help_intro:'Pulsa un botón ? para ver qué representa cada elemento.',chart_aria:'Gráfica temporal del score, el umbral y los hitos de detección',chart_waiting:'Esperando muestras para dibujar la gráfica.',manual_recalibration:'Recalibración manual',calibration_intro:'Programa una espera, sal de la habitación y déjala vacía mientras ambos detectores se calibran.',start_in:'Comenzar en',schedule_recalibration:'Programar recalibración',no_recalibration:'Sin recalibración programada.',scan_networks:'Buscar redes cercanas',scan_not_started:'Escaneo todavía no iniciado.',wifi_networks_aria:'Redes Wi-Fi encontradas',selected_ssid:'SSID seleccionado o manual',network_password:'Contraseña de la red',keep_password_placeholder:'en blanco conserva la contraseña actual',open_or_clear:'Red abierta o borrar contraseña guardada',wifi_password_intro:'La contraseña guardada se conservará si mantienes el SSID y dejas el campo vacío.',rssi_detector:'Detector RSSI',save_notice:'Los cambios de este bloque solo se aplican al guardar y reiniciar.',detection_source:'Fuente de detección',rssi_only:'Solo RSSI',csi_only:'Solo CSI',rssi_or_csi:'RSSI o CSI',profile:'Perfil',low_sensitivity:'Baja sensibilidad',balanced:'Equilibrado',high_sensitivity:'Alta sensibilidad',algorithm:'Algoritmo',mean_difference:'Diferencia media',standard_deviation:'Desviación estándar',variance:'Varianza',range:'Rango máximo-mínimo',median_deviation:'Desviación mediana',baseline:'Referencia (baseline)',mean_stddev:'Media y desviación',robust_median:'Mediana robusta',config_help_intro:'Pulsa un botón ? para conocer estas opciones.',interval:'Intervalo (ms)',window:'Ventana',calibration:'Calibración',sigma_multiplier:'Multiplicador sigma',minimum_threshold:'Umbral mínimo',release_ratio:'Ratio liberación',baseline_adaptation:'Adaptación baseline',trigger_confirmation:'Confirmación activación',release_confirmation:'Confirmación liberación',no_pending_changes:'Sin cambios pendientes.',save_restart:'Guardar Wi-Fi y detector; reiniciar',import_export:'Importar / exportar',import:'Importar',export:'Exportar',factory_reset:'Restaurar fábrica',telegram_notifications:'Notificaciones de Telegram',telegram_intro:'Envía cada entrada en detección mediante una cola independiente. El token nunca se vuelve a mostrar.',enable_notifications:'Habilitar avisos',bot_token:'Token del bot',keep_token_placeholder:'en blanco conserva el token guardado',clear_token:'Borrar token guardado',save_telegram:'Guardar ajustes de Telegram',test_telegram:'Enviar mensaje de prueba',telegram_not_loaded:'Telegram sin consultar.',admin_password:'Contraseña de administración',current_password:'Contraseña actual',new_password:'Nueva contraseña',change_password:'Cambiar contraseña',diagnostics:'Diagnóstico',help_rssi_score_label:'Ayuda sobre Score RSSI',help_rssi_threshold_label:'Ayuda sobre Umbral RSSI',help_csi_score_label:'Ayuda sobre Score CSI',help_csi_threshold_label:'Ayuda sobre Umbral CSI',help_rssi_hits_label:'Ayuda sobre detecciones RSSI',help_csi_hits_label:'Ayuda sobre detecciones CSI',help_source_label:'Ayuda sobre Fuente de detección',help_profile_label:'Ayuda sobre Perfil',help_algorithm_label:'Ayuda sobre Algoritmo',help_baseline_label:'Ayuda sobre Referencia baseline',help_rssi_score:'Variación reciente de la potencia Wi-Fi recibida. Cuanto más sube, mayor cambio observa el detector RSSI.',help_rssi_threshold:'Nivel que debe superar el score RSSI durante varias muestras para declarar movimiento.',help_csi_score:'Variación detallada del canal Wi-Fi calculada con CSI. Puede controlar el estado principal al seleccionar Solo CSI o RSSI o CSI.',help_csi_threshold:'Límite adaptativo del detector CSI. Una subida aislada no basta: también exige muestras consecutivas.',help_rssi_hits:'Línea roja numerada que marca cada entrada del detector RSSI en estado de movimiento.',help_csi_hits:'Línea violeta C# que marca una entrada en detección CSI. Controla la tarjeta principal y el LED cuando CSI forma parte de la fuente seleccionada.',help_source:'Elige qué detector controla la tarjeta principal, el LED y los avisos. RSSI o CSI declara movimiento si cualquiera de los dos detecta.',help_profile:'El perfil ajusta conjuntamente la sensibilidad y los tiempos de confirmación. Baja reduce falsas alarmas; Alta reacciona antes; Equilibrado es el punto de partida recomendado.',help_algorithm:'Define cómo se resume la variación de las muestras recientes. Cambiarlo modifica el score RSSI; el detector CSI usa su propia configuración.',help_baseline:'Es el nivel normal de una habitación quieta. Media y desviación responde mejor a datos regulares; Mediana robusta tolera mejor picos aislados.',default_password_warning:'Aviso: sigues usando la contraseña inicial admin. Cámbiala abajo.',admin_session:'Sesión de admin activa. Esta web usa HTTP local; no la expongas a Internet.',session_expired:'La sesión no es válida o ha caducado.',auth_required:'Autenticación requerida',checking:'Comprobando...',session_closed:'Sesión cerrada.',saving:'Guardando...',wifi_password_set:'Hay una contraseña guardada. Se conservará mientras no cambies de SSID ni marques la casilla de borrado.',wifi_no_password:'La red actual no tiene contraseña guardada.',recovery_mode:'Modo recuperación AP',local_connected:'Conectado a la red local',new_network_password:'Introduce la contraseña de la nueva red o marca que es una red abierta.',wifi_will_clear:'Al guardar se eliminará la contraseña Wi-Fi almacenada.',wifi_only_if_new:'La contraseña solo se cambia si escribes una nueva.',current_protected:'Red actual protegida: el campo vacío conserva su contraseña.',protected_password:'Red protegida: introduce su contraseña antes de guardar.',open_network:'Red abierta: se guardará sin contraseña.',current_selected:'Red actual seleccionada; sin cambios pendientes.',new_selected:'Nueva red seleccionada; falta guardar y reiniciar.',current_suffix:' (actual)',channel:'canal',unsupported:'no compatible',no_networks:'No se encontraron redes visibles.',scanning:'Buscando redes; la detección está pausada para evitar falsas alarmas...',networks_found:'Redes visibles encontradas: {n}.',scan_error:'Error de escaneo: {error}',starting_scan:'Iniciando búsqueda...',token_saved:'Token guardado. ',no_token:'Sin token guardado. ',automatic_on:'Avisos automáticos activos sobre {source}. ',automatic_off:'Avisos automáticos desactivados. ',health:'Enviados: {sent} · fallos: {failures} · descartados: {dropped} · cola: {queue}',last_http:' · último HTTP: {status}',saving_telegram:'Guardando Telegram...',test_sent:'Mensaje de prueba enviado correctamente. Total enviados: {n}',test_failed:'Telegram rechazó o no pudo enviar el mensaje. HTTP {status} · {error}',test_queued:'Mensaje en cola; esperando respuesta de Telegram...',pending_changes:'Cambios pendientes: todavía no están aplicados.',saving_restart:'Guardando y preparando el reinicio...',restarting:'Reiniciando...',saved_reconnect:'Configuración guardada. Reconectando en 7 segundos...',save_failed:'No se pudo guardar: {error}',factory_confirm:'¿Borrar configuración y credenciales Wi-Fi?',recalibration_scheduled:'Recalibración programada. Sal de la habitación.',motion_detected:'MOVIMIENTO DETECTADO',no_motion:'Sin movimiento',read_error:'Error de lectura',unknown_state:'Estado desconocido',calibration_countdown:'Recalibración comienza en {seconds} s. Sal de la habitación.',calibrating_now:'Calibrando ahora. Mantén la habitación vacía.',calibration_done:'Calibración terminada; detectores listos.',chart_summary:'Series visibles: {shown} · detecciones visibles RSSI: {rssi} · CSI: {csi}',hidden:'ocultas',no_connection:'Sin conexión',device_unreachable:'No se pudo contactar con el dispositivo.',rssi_or_csi_source:'RSSI o CSI'}};"
    "function loadLanguage(){try{return localStorage.getItem('motion_language')==='es'?'es':'en'}catch(e){return'en'}}let language=loadLanguage();"
    "function tr(key,values={}){let value=T[language][key]||T.en[key]||key;return value.replace(/\\{(\\w+)\\}/g,(m,name)=>values[name]??m)}"
    "function message(element,key,values={}){let safe=Object.fromEntries(Object.entries(values).map(([name,value])=>[name,String(value)]));element.dataset.messageKey=key;element.dataset.messageValues=JSON.stringify(safe);element.textContent=tr(key,safe)}"
    "function setDirectText(element,value){let node=[...element.childNodes].find(n=>n.nodeType===3&&n.nodeValue.trim());if(node)node.nodeValue=value;else element.textContent=value}"
    "function applyLanguage(){document.documentElement.lang=language;for(let element of document.querySelectorAll('[data-i18n]'))setDirectText(element,tr(element.dataset.i18n));for(let element of document.querySelectorAll('[data-i18n-aria]'))element.setAttribute('aria-label',tr(element.dataset.i18nAria));for(let element of document.querySelectorAll('[data-i18n-placeholder]'))element.placeholder=tr(element.dataset.i18nPlaceholder);for(let button of document.querySelectorAll('[data-aria-key]')){let label=tr(button.dataset.ariaKey);button.setAttribute('aria-label',label);button.title=label}for(let element of document.querySelectorAll('[data-message-key]'))element.textContent=tr(element.dataset.messageKey,JSON.parse(element.dataset.messageValues||'{}'));document.getElementById('lang_en').setAttribute('aria-pressed',String(language==='en'));document.getElementById('lang_es').setAttribute('aria-pressed',String(language==='es'));if(typeof drawChart==='function')drawChart();if(typeof lastWifiScan!=='undefined'&&lastWifiScan)renderNetworks(lastWifiScan);if(typeof lastTelegram!=='undefined'&&lastTelegram)renderTelegram(lastTelegram,false);if(typeof lastDiagnostics!=='undefined'&&lastDiagnostics){renderMotion(lastDiagnostics);renderCalibration(lastDiagnostics.runtime)}}"
    "function changeLanguage(next){language=next==='es'?'es':'en';try{localStorage.setItem('motion_language',language)}catch(e){}applyLanguage()}"
    "const ids=['ssid','profile','algorithm','baseline_mode','detection_source','interval_ms','window_size','calibration_samples','sigma_multiplier','minimum_threshold','release_threshold_ratio','baseline_update_alpha','trigger_consecutive','clear_consecutive'];"
    "const chartToggleIds=['show_rssi_score','show_rssi_threshold','show_csi_score','show_csi_threshold','show_rssi_hits','show_csi_hits'];"
    "const ui=Object.fromEntries([...ids,...chartToggleIds,'password','clear_password','status','f','file','import','reset','diag','motion_card','motion_label','motion_detail','motion_chart','chart_help','chart_summary','calibration_delay','start_calibration','calibration_status','config_help','config_save_status','save_config','login_panel','login_form','login_username','login_password','login_status','app','security_notice','logout','password_form','current_admin_password','new_admin_password','password_status','scan_wifi','wifi_scan_status','wifi_networks','wifi_password_status','telegram_form','telegram_enabled','telegram_token','telegram_clear_token','telegram_chat_id','save_telegram','test_telegram','telegram_status'].map(id=>[id,document.getElementById(id)]));"
    "const HISTORY_MS=120000,chartPoints=[];let lastMotion=false,lastCsiMotion=false,detectionCount=0,csiDetectionCount=0,authenticated=false,loadedSsid='',selectedNetworkSecure=null,lastWifiScan=null,lastTelegram=null,lastDiagnostics=null;"
    "document.getElementById('lang_en').onclick=()=>changeLanguage('en');document.getElementById('lang_es').onclick=()=>changeLanguage('es');applyLanguage();"
    "function showLogin(message=''){authenticated=false;ui.app.hidden=true;ui.login_panel.hidden=false;ui.login_status.textContent=message;ui.login_password.value='';ui.login_password.focus()}"
    "async function showApp(session){authenticated=true;ui.login_panel.hidden=true;ui.app.hidden=false;message(ui.security_notice,session.default_password?'default_password_warning':'admin_session');await load();await loadTelegram();diagnostic()}"
    "async function authFetch(url,options={}){let r=await fetch(url,options);if(r.status===401){showLogin(tr('session_expired'));throw Error(tr('auth_required'))}return r}"
    "ui.login_form.onsubmit=async e=>{e.preventDefault();ui.login_status.textContent=tr('checking');try{let r=await fetch('/api/login',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({username:'admin',password:ui.login_password.value})}),t=await r.text();if(!r.ok)throw Error(t);await showApp(JSON.parse(t))}catch(x){ui.login_status.textContent=String(x)}};"
    "ui.logout.onclick=async()=>{try{await authFetch('/api/logout',{method:'POST'});}catch(x){}showLogin(tr('session_closed'))};"
    "ui.password_form.onsubmit=async e=>{e.preventDefault();ui.password_status.textContent=tr('saving');try{let r=await authFetch('/api/password',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({current_password:ui.current_admin_password.value,new_password:ui.new_admin_password.value})}),t=await r.text();if(!r.ok)throw Error(t);ui.current_admin_password.value='';ui.new_admin_password.value='';showLogin(t)}catch(x){ui.password_status.textContent=String(x)}};"
    "function setNumberInput(input,value){let step=Number(input.step)||1,decimals=(input.step.split('.')[1]||'').length;input.value=(Math.round(Number(value)/step)*step).toFixed(decimals)}"
    "async function load(){let r=await authFetch('/api/config'),c=await r.json(),d=c.detector;loadedSsid=c.wifi.ssid;selectedNetworkSecure=null;ui.ssid.value=loadedSsid;ui.password.value='';ui.password.required=false;ui.clear_password.checked=false;message(ui.wifi_password_status,c.wifi.password_set?'wifi_password_set':'wifi_no_password');ui.profile.value=d.profile;ui.algorithm.value=d.algorithm;ui.baseline_mode.value=d.baseline_mode;ui.detection_source.value=d.source;setNumberInput(ui.interval_ms,c.sampling.interval_ms);for(let k of ids.slice(6))setNumberInput(ui[k],d[k]);message(ui.status,c.provisioning_mode?'recovery_mode':'local_connected');message(ui.config_save_status,'no_pending_changes');}"
    "function payload(){let d={};for(let k of ids.slice(6))d[k]=ui[k].valueAsNumber;d.profile=ui.profile.value;d.algorithm=ui.algorithm.value;d.baseline_mode=ui.baseline_mode.value;d.source=ui.detection_source.value;let changed=ui.ssid.value!==loadedSsid,p={schema_version:4,wifi:{ssid:ui.ssid.value},sampling:{interval_ms:ui.interval_ms.valueAsNumber},detector:d};if(ui.clear_password.checked)p.wifi.password='';else if(ui.password.value)p.wifi.password=ui.password.value;else if(changed&&selectedNetworkSecure===false)p.wifi.password='';else if(changed)throw Error(tr('new_network_password'));return p;}"
    "ui.ssid.oninput=()=>{if(ui.ssid.value!==loadedSsid)selectedNetworkSecure=null;ui.password.required=false};"
    "ui.clear_password.onchange=()=>{ui.password.required=false;message(ui.wifi_password_status,ui.clear_password.checked?'wifi_will_clear':'wifi_only_if_new')};"
    "function chooseNetwork(n){if(!n.supported)return;ui.ssid.value=n.ssid;selectedNetworkSecure=n.password_required;ui.password.value='';ui.clear_password.checked=!n.password_required;ui.password.required=n.password_required&&n.ssid!==loadedSsid;message(ui.wifi_password_status,n.password_required?(n.ssid===loadedSsid?'current_protected':'protected_password'):'open_network');for(let b of ui.wifi_networks.querySelectorAll('button'))b.setAttribute('aria-pressed',String(b.dataset.ssid===n.ssid));message(ui.config_save_status,n.ssid===loadedSsid?'current_selected':'new_selected')}"
    "function renderNetworks(data){lastWifiScan=data;ui.wifi_networks.replaceChildren();for(let n of data.networks){let li=document.createElement('li'),b=document.createElement('button'),name=document.createElement('span'),meta=document.createElement('span');b.type='button';b.dataset.ssid=n.ssid;b.disabled=!n.supported;b.setAttribute('aria-pressed',String(ui.ssid.value===n.ssid));name.textContent=n.ssid+(n.selected?tr('current_suffix'):'');meta.className='wifi-network-meta';meta.textContent=n.rssi+' dBm · '+tr('channel')+' '+n.channel+' · '+n.security+(n.supported?'':' · '+tr('unsupported'));b.append(name,meta);b.onclick=()=>chooseNetwork(n);li.append(b);ui.wifi_networks.append(li)}if(!data.networks.length&&data.state==='ready')message(ui.wifi_scan_status,'no_networks')}"
    "async function pollWifiScan(){try{let r=await authFetch('/api/wifi/scan',{cache:'no-store'});if(!r.ok)throw Error(await r.text());let data=await r.json();renderNetworks(data);if(data.state==='scanning'){message(ui.wifi_scan_status,'scanning');setTimeout(pollWifiScan,500)}else if(data.state==='ready'){ui.scan_wifi.disabled=false;message(ui.wifi_scan_status,'networks_found',{n:data.networks.length})}else if(data.state==='error'){ui.scan_wifi.disabled=false;message(ui.wifi_scan_status,'scan_error',{error:data.error})}}catch(x){ui.scan_wifi.disabled=false;ui.wifi_scan_status.textContent=String(x)}}"
    "ui.scan_wifi.onclick=async()=>{ui.scan_wifi.disabled=true;message(ui.wifi_scan_status,'starting_scan');try{let r=await authFetch('/api/wifi/scan',{method:'POST'});if(!r.ok)throw Error(await r.text());pollWifiScan()}catch(x){ui.scan_wifi.disabled=false;ui.wifi_scan_status.textContent=String(x)}};"
    "function sourceName(value){return value==='both'?tr('rssi_or_csi_source'):value.toUpperCase()}function renderTelegram(t,updateInputs=true){lastTelegram=t;if(updateInputs){ui.telegram_enabled.checked=t.enabled;ui.telegram_chat_id.value=t.chat_id||'';ui.telegram_token.value='';ui.telegram_clear_token.checked=false}let source=sourceName(ui.detection_source.value),automatic=tr(t.enabled?'automatic_on':'automatic_off',{source}),health=tr('health',{sent:t.sent,failures:t.failures,dropped:t.dropped,queue:t.queue_depth});ui.telegram_status.textContent=tr(t.token_set?'token_saved':'no_token')+automatic+health+(t.last_http_status?tr('last_http',{status:t.last_http_status}):'')}"
    "async function loadTelegram(){try{let r=await authFetch('/api/telegram',{cache:'no-store'});if(!r.ok)throw Error(await r.text());renderTelegram(await r.json())}catch(x){ui.telegram_status.textContent=String(x)}}"
    "ui.telegram_form.onsubmit=async e=>{e.preventDefault();ui.save_telegram.disabled=true;ui.telegram_status.textContent=tr('saving_telegram');let p={enabled:ui.telegram_enabled.checked,chat_id:ui.telegram_chat_id.value,clear_token:ui.telegram_clear_token.checked};if(ui.telegram_token.value)p.token=ui.telegram_token.value;try{let r=await authFetch('/api/telegram',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(p)}),text=await r.text();if(!r.ok)throw Error(text);renderTelegram(JSON.parse(text))}catch(x){ui.telegram_status.textContent=String(x)}ui.save_telegram.disabled=false};"
    "async function pollTelegramTest(sent,failures,attempt=0){let r=await authFetch('/api/telegram',{cache:'no-store'}),t=await r.json();renderTelegram(t);if((t.busy||t.queue_depth)&&attempt<12)return setTimeout(()=>pollTelegramTest(sent,failures,attempt+1),1000);if(t.sent>sent)ui.telegram_status.textContent=tr('test_sent',{n:t.sent});else if(t.failures>failures)ui.telegram_status.textContent=tr('test_failed',{status:t.last_http_status,error:t.last_error})}"
    "ui.test_telegram.onclick=async()=>{ui.test_telegram.disabled=true;try{let before=await (await authFetch('/api/telegram',{cache:'no-store'})).json(),r=await authFetch('/api/telegram/test',{method:'POST'}),text=await r.text();if(!r.ok)throw Error(text);ui.telegram_status.textContent=tr('test_queued');setTimeout(()=>pollTelegramTest(before.sent,before.failures),500)}catch(x){ui.telegram_status.textContent=String(x)}finally{ui.test_telegram.disabled=false}};"
    "ui.profile.onchange=()=>{let p={low:[8,.75,.01,4,10],balanced:[6,.75,.01,3,8],high:[4,.70,.02,2,5]}[ui.profile.value];ui.sigma_multiplier.value=p[0];ui.release_threshold_ratio.value=p[1];ui.baseline_update_alpha.value=p[2];ui.trigger_consecutive.value=p[3];ui.clear_consecutive.value=p[4]};"
    "async function send(url,p){let r=await authFetch(url,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(p)});let t=await r.text();if(!r.ok)throw Error(t);ui.status.textContent=t;}"
    "ui.f.oninput=()=>{message(ui.config_save_status,'pending_changes')};"
    "ui.f.onsubmit=async e=>{e.preventDefault();ui.save_config.disabled=true;ui.save_config.textContent=tr('saving');ui.config_save_status.textContent=tr('saving_restart');try{await send('/api/config',payload());ui.save_config.textContent=tr('restarting');ui.config_save_status.textContent=tr('saved_reconnect');setTimeout(()=>location.reload(),7000)}catch(x){ui.status.textContent=x;ui.config_save_status.textContent=tr('save_failed',{error:x});ui.save_config.disabled=false;ui.save_config.textContent=tr('save_restart')}};"
    "ui.import.onclick=async()=>{try{let x=JSON.parse(await ui.file.files[0].text());await send('/api/import',x)}catch(x){ui.status.textContent=x}};"
    "ui.reset.onclick=async()=>{if(confirm(tr('factory_confirm')))try{await send('/api/factory-reset',{})}catch(x){ui.status.textContent=x}};"
    "for(let b of document.querySelectorAll('.help')){let show=()=>document.getElementById(b.dataset.target||'chart_help').textContent=tr(b.dataset.helpKey);b.onclick=show;b.onfocus=show}"
    "ui.start_calibration.onclick=async()=>{let delay=Number(ui.calibration_delay.value);try{await send('/api/calibrate',{delay_seconds:delay});ui.calibration_status.textContent=tr('recalibration_scheduled')}catch(x){ui.calibration_status.textContent=String(x)}};"
    "function renderMotion(d){let r=d.runtime,c=r.csi_detector||{},ok=r.detection_source==='csi'||r.sample_ok,s=!ok?'error':r.selected_state,m={motion:['motion_detected','motion'],idle:['no_motion','idle'],calibrating:['calibrating_detector','calibrating'],error:['read_error','error']}[s]||['unknown_state','error'],source=sourceName(r.detection_source);ui.motion_card.className='motion-card '+m[1];ui.motion_label.textContent=tr(m[0]);ui.motion_detail.textContent=source+' · RSSI '+Number(r.score).toFixed(3)+' / '+Number(r.threshold).toFixed(3)+' · CSI '+Number(c.score).toFixed(3)+' / '+Number(c.threshold).toFixed(3)}"
    "function renderCalibration(r){let c=r.csi_detector||{};if(r.calibration_pending){let seconds=Math.max(0,Math.ceil(Number(r.calibration_remaining_ms)/1000));ui.calibration_status.textContent=tr('calibration_countdown',{seconds})}else if(r.state==='calibrating'||c.state==='calibrating'){ui.calibration_status.textContent=tr('calibrating_now')}else{ui.calibration_status.textContent=tr('calibration_done')}}"
    "function addChartPoint(r){let now=Date.now(),motion=r.sample_ok&&r.state==='motion',hit=motion&&!lastMotion,c=r.csi_detector||{},csiMotion=c.calibrated&&c.state==='motion',csiHit=csiMotion&&!lastCsiMotion;if(hit)detectionCount++;if(csiHit)csiDetectionCount++;lastMotion=motion;lastCsiMotion=csiMotion;chartPoints.push({t:now,score:Number(r.score)||0,threshold:Number(r.threshold)||0,csiScore:c.calibrated?Number(c.score):NaN,csiThreshold:c.calibrated?Number(c.threshold):NaN,hit:hit,hitNo:detectionCount,csiHit:csiHit,csiHitNo:csiDetectionCount});while(chartPoints.length&&chartPoints[0].t<now-HISTORY_MS)chartPoints.shift();drawChart()}"
    "function drawChart(){let c=ui.motion_chart,box=c.getBoundingClientRect(),dpr=Math.min(devicePixelRatio||1,2),w=Math.max(240,box.width),h=Math.max(220,box.height);if(c.width!==Math.round(w*dpr)||c.height!==Math.round(h*dpr)){c.width=Math.round(w*dpr);c.height=Math.round(h*dpr)}let x=c.getContext('2d');x.setTransform(dpr,0,0,dpr,0,0);x.clearRect(0,0,w,h);let p={l:44,r:12,t:12,b:34},pw=w-p.l-p.r,ph=h-p.t-p.b,now=Date.now(),start=now-HISTORY_MS,keys=[];if(ui.show_rssi_score.checked)keys.push('score');if(ui.show_rssi_threshold.checked)keys.push('threshold');if(ui.show_csi_score.checked)keys.push('csiScore');if(ui.show_csi_threshold.checked)keys.push('csiThreshold');let values=chartPoints.flatMap(v=>keys.map(k=>v[k]).filter(Number.isFinite)),max=Math.max(.1,...values)*1.15,xx=t=>p.l+Math.max(0,(t-start)/HISTORY_MS)*pw,yy=v=>p.t+ph-Math.min(1,v/max)*ph;x.font='11px system-ui';x.fillStyle='#58616c';x.strokeStyle='#d9dee5';x.lineWidth=1;for(let i=0;i<=4;i++){let y=p.t+ph*i/4;x.beginPath();x.moveTo(p.l,y);x.lineTo(w-p.r,y);x.stroke();x.fillText((max*(1-i/4)).toFixed(2),3,y+4)}for(let i=0;i<=4;i++){let t=start+HISTORY_MS*i/4,px=p.l+pw*i/4;x.beginPath();x.moveTo(px,p.t);x.lineTo(px,p.t+ph);x.stroke();let label=new Date(t).toLocaleTimeString(language==='es'?'es-ES':'en-GB',{hour:'2-digit',minute:'2-digit',second:'2-digit'});x.fillText(label,Math.max(p.l,Math.min(px-24,w-62)),h-9)}for(let v of chartPoints){let px=xx(v.t);if(v.hit&&ui.show_rssi_hits.checked){x.strokeStyle='#c62828';x.lineWidth=2;x.setLineDash([]);x.beginPath();x.moveTo(px,p.t);x.lineTo(px,p.t+ph);x.stroke();x.fillStyle='#c62828';x.fillText('#'+v.hitNo,px+3,p.t+11)}if(v.csiHit&&ui.show_csi_hits.checked){x.strokeStyle='#7c3aed';x.lineWidth=2;x.setLineDash([3,3]);x.beginPath();x.moveTo(px,p.t);x.lineTo(px,p.t+ph);x.stroke();x.setLineDash([]);x.fillStyle='#7c3aed';x.fillText('C#'+v.csiHitNo,px+3,p.t+25)}}function line(key,color,dash,visible){if(!visible)return;x.strokeStyle=color;x.lineWidth=2;x.setLineDash(dash);x.beginPath();let first=true;for(let v of chartPoints){if(!Number.isFinite(v[key])){first=true;continue}let px=xx(v.t),py=yy(v[key]);first?(x.moveTo(px,py),first=false):x.lineTo(px,py)}x.stroke();x.setLineDash([])}line('threshold','#d97706',[6,4],ui.show_rssi_threshold.checked);line('score','#1769aa',[],ui.show_rssi_score.checked);line('csiThreshold','#db2777',[3,4],ui.show_csi_threshold.checked);line('csiScore','#7c3aed',[],ui.show_csi_score.checked);let hits=chartPoints.filter(v=>v.hit).length,csiHits=chartPoints.filter(v=>v.csiHit).length,last=chartPoints.at(-1),shown=keys.length;ui.chart_summary.textContent=last?tr('chart_summary',{shown,rssi:ui.show_rssi_hits.checked?hits:tr('hidden'),csi:ui.show_csi_hits.checked?csiHits:tr('hidden')}):tr('chart_waiting')}"
    "for(let id of chartToggleIds)ui[id].onchange=drawChart;"
    "addEventListener('resize',drawChart);"
    "async function diagnostic(){if(!authenticated)return;try{let r=await authFetch('/api/diagnostics',{cache:'no-store'});if(!r.ok)throw Error('HTTP '+r.status);let d=await r.json();lastDiagnostics=d;ui.diag.textContent=JSON.stringify(d,null,2);renderMotion(d);renderCalibration(d.runtime);addChartPoint(d.runtime)}catch(e){if(!authenticated)return;ui.motion_card.className='motion-card error';message(ui.motion_label,'no_connection');ui.motion_detail.textContent=String(e);ui.diag.textContent=e}if(authenticated)setTimeout(diagnostic,500)}"
    "async function boot(){try{let r=await fetch('/api/session',{cache:'no-store'});if(!r.ok)return showLogin();await showApp(await r.json())}catch(x){showLogin(tr('device_unreachable'))}}boot();"
    "</script></html>";

static cJSON *config_json(const app_runtime_config_t *config,
                          bool include_secrets)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }
    cJSON *wifi = cJSON_AddObjectToObject(root, "wifi");
    cJSON *sampling = cJSON_AddObjectToObject(root, "sampling");
    cJSON *detector = cJSON_AddObjectToObject(root, "detector");
    if (wifi == NULL || sampling == NULL || detector == NULL) {
        cJSON_Delete(root);
        return NULL;
    }

    cJSON_AddNumberToObject(root, "schema_version", APP_CONFIG_SCHEMA_VERSION);
    cJSON_AddBoolToObject(root, "provisioning_mode", current_provisioning_mode);
    cJSON_AddStringToObject(wifi, "ssid", config->wifi_ssid);
    cJSON_AddBoolToObject(wifi,
                          "password_set",
                          config->wifi_password[0] != '\0');
    if (include_secrets) {
        cJSON_AddStringToObject(wifi, "password", config->wifi_password);
    }
    cJSON_AddNumberToObject(sampling,
                            "interval_ms",
                            config->sample_interval_ms);
    cJSON_AddStringToObject(
        detector,
        "source",
        app_detection_source_name(config->detection_source));
    cJSON_AddStringToObject(
        detector,
        "profile",
        motion_sensitivity_profile_name(config->sensitivity_profile));
    cJSON_AddStringToObject(
        detector,
        "algorithm",
        motion_feature_algorithm_name(config->detector.algorithm));
    cJSON_AddStringToObject(
        detector,
        "baseline_mode",
        motion_baseline_mode_name(config->detector.baseline_mode));
    cJSON_AddNumberToObject(detector, "window_size", config->detector.window_size);
    cJSON_AddNumberToObject(detector,
                            "calibration_samples",
                            config->detector.calibration_samples);
    cJSON_AddNumberToObject(detector,
                            "sigma_multiplier",
                            config->detector.sigma_multiplier);
    cJSON_AddNumberToObject(detector,
                            "minimum_threshold",
                            config->detector.minimum_threshold);
    cJSON_AddNumberToObject(detector,
                            "release_threshold_ratio",
                            config->detector.release_threshold_ratio);
    cJSON_AddNumberToObject(detector,
                            "baseline_update_alpha",
                            config->detector.baseline_update_alpha);
    cJSON_AddNumberToObject(detector,
                            "trigger_consecutive",
                            config->detector.trigger_consecutive);
    cJSON_AddNumberToObject(detector,
                            "clear_consecutive",
                            config->detector.clear_consecutive);
    return root;
}

static esp_err_t send_json(httpd_req_t *request, cJSON *json)
{
    if (json == NULL) {
        return httpd_resp_send_err(request,
                                   HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "out of memory");
    }
    char *body = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (body == NULL) {
        return httpd_resp_send_err(request,
                                   HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "out of memory");
    }
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "Connection", "close");
    const esp_err_t error = httpd_resp_sendstr(request, body);
    free(body);
    return error;
}

static esp_err_t index_handler(httpd_req_t *request)
{
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(request, "Referrer-Policy", "no-referrer");
    httpd_resp_set_hdr(request,
                       "Content-Security-Policy",
                       "default-src 'self'; script-src 'unsafe-inline'; "
                       "style-src 'unsafe-inline'; img-src 'self' data:; "
                       "connect-src 'self'; frame-ancestors 'none'");
    httpd_resp_set_hdr(request, "Connection", "close");
    return httpd_resp_send(request, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t captive_redirect_handler(httpd_req_t *request)
{
    if (!current_provisioning_mode) {
        return httpd_resp_send_err(request,
                                   HTTPD_404_NOT_FOUND,
                                   "not found");
    }
    httpd_resp_set_status(request, "302 Found");
    httpd_resp_set_hdr(request, "Location", "/");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, "Open the configuration portal");
}

static bool request_session_token(httpd_req_t *request,
                                  char token[
                                      PORTAL_AUTH_SESSION_TOKEN_HEX_LENGTH + 1U])
{
    const size_t cookie_length = httpd_req_get_hdr_value_len(request, "Cookie");
    if (cookie_length == 0U || cookie_length >= SESSION_COOKIE_MAX_LENGTH) {
        return false;
    }
    char cookie[SESSION_COOKIE_MAX_LENGTH];
    if (httpd_req_get_hdr_value_str(request,
                                    "Cookie",
                                    cookie,
                                    sizeof(cookie)) != ESP_OK) {
        return false;
    }
    const char prefix[] = SESSION_COOKIE_NAME "=";
    char *entry = cookie;
    while (entry != NULL && *entry != '\0') {
        while (*entry == ' ') {
            ++entry;
        }
        char *end = strchr(entry, ';');
        const size_t entry_length = end != NULL
            ? (size_t)(end - entry)
            : strlen(entry);
        if (entry_length == sizeof(prefix) - 1U +
                                PORTAL_AUTH_SESSION_TOKEN_HEX_LENGTH &&
            memcmp(entry, prefix, sizeof(prefix) - 1U) == 0) {
            memcpy(token,
                   entry + sizeof(prefix) - 1U,
                   PORTAL_AUTH_SESSION_TOKEN_HEX_LENGTH);
            token[PORTAL_AUTH_SESSION_TOKEN_HEX_LENGTH] = '\0';
            return true;
        }
        entry = end != NULL ? end + 1 : NULL;
    }
    return false;
}

static bool request_authenticated(httpd_req_t *request)
{
    char token[PORTAL_AUTH_SESSION_TOKEN_HEX_LENGTH + 1U];
    return request_session_token(request, token) &&
           portal_auth_session_valid(token, esp_timer_get_time() / 1000);
}

static esp_err_t require_authenticated(httpd_req_t *request)
{
    if (request_authenticated(request)) {
        return ESP_OK;
    }
    httpd_resp_set_status(request, "401 Unauthorized");
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request,
                              "{\"error\":\"authentication required\"}");
}

static esp_err_t get_config_handler(httpd_req_t *request)
{
    if (!request_authenticated(request)) {
        return require_authenticated(request);
    }
    return send_json(request, config_json(&current_config, false));
}

static bool export_includes_secrets(httpd_req_t *request)
{
    char query[64];
    char value[8];
    return httpd_req_get_url_query_str(request, query, sizeof(query)) == ESP_OK &&
           httpd_query_key_value(query,
                                 "include_secrets",
                                 value,
                                 sizeof(value)) == ESP_OK &&
           strcmp(value, "1") == 0;
}

static esp_err_t export_handler(httpd_req_t *request)
{
    if (!request_authenticated(request)) {
        return require_authenticated(request);
    }
    httpd_resp_set_hdr(request,
                       "Content-Disposition",
                       "attachment; filename=wifi-motion-config.json");
    return send_json(request,
                     config_json(&current_config,
                                 export_includes_secrets(request)));
}

static bool json_integer(const cJSON *object,
                         const char *name,
                         size_t *destination)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsNumber(item) || item->valuedouble < 0.0 ||
        item->valuedouble > (double)UINT32_MAX ||
        floor(item->valuedouble) != item->valuedouble) {
        return false;
    }
    *destination = (size_t)item->valuedouble;
    return true;
}

static bool json_float(const cJSON *object,
                       const char *name,
                       float *destination)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) {
        return false;
    }
    *destination = (float)item->valuedouble;
    return true;
}

static bool parse_profile(const char *name,
                          motion_sensitivity_profile_t *profile)
{
    for (int value = 0; value < MOTION_PROFILE_COUNT; ++value) {
        if (strcmp(name,
                   motion_sensitivity_profile_name(
                       (motion_sensitivity_profile_t)value)) == 0) {
            *profile = (motion_sensitivity_profile_t)value;
            return true;
        }
    }
    return false;
}

static bool parse_algorithm(const char *name,
                            motion_feature_algorithm_t *algorithm)
{
    for (int value = 0; value < MOTION_FEATURE_COUNT; ++value) {
        if (strcmp(name,
                   motion_feature_algorithm_name(
                       (motion_feature_algorithm_t)value)) == 0) {
            *algorithm = (motion_feature_algorithm_t)value;
            return true;
        }
    }
    return false;
}

static bool parse_baseline(const char *name, motion_baseline_mode_t *mode)
{
    for (int value = 0; value < MOTION_BASELINE_COUNT; ++value) {
        if (strcmp(name,
                   motion_baseline_mode_name(
                       (motion_baseline_mode_t)value)) == 0) {
            *mode = (motion_baseline_mode_t)value;
            return true;
        }
    }
    return false;
}

static bool parse_config_json(const cJSON *root,
                              app_runtime_config_t *candidate)
{
    const cJSON *schema = cJSON_GetObjectItemCaseSensitive(root,
                                                           "schema_version");
    const cJSON *wifi = cJSON_GetObjectItemCaseSensitive(root, "wifi");
    const cJSON *sampling = cJSON_GetObjectItemCaseSensitive(root, "sampling");
    const cJSON *detector = cJSON_GetObjectItemCaseSensitive(root, "detector");
    const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(wifi, "ssid");
    const cJSON *password = cJSON_GetObjectItemCaseSensitive(wifi, "password");
    const cJSON *profile = cJSON_GetObjectItemCaseSensitive(detector, "profile");
    const cJSON *source = cJSON_GetObjectItemCaseSensitive(detector, "source");
    const cJSON *algorithm = cJSON_GetObjectItemCaseSensitive(detector,
                                                              "algorithm");
    const cJSON *baseline = cJSON_GetObjectItemCaseSensitive(detector,
                                                             "baseline_mode");
    size_t interval = 0U;

    if (!cJSON_IsNumber(schema) ||
        schema->valuedouble != (double)APP_CONFIG_SCHEMA_VERSION ||
        !cJSON_IsObject(wifi) || !cJSON_IsObject(sampling) ||
        !cJSON_IsObject(detector) || !cJSON_IsString(ssid) ||
        strlen(ssid->valuestring) > APP_CONFIG_WIFI_SSID_MAX_LENGTH ||
        (password != NULL &&
         (!cJSON_IsString(password) ||
          strlen(password->valuestring) > APP_CONFIG_WIFI_PASSWORD_MAX_LENGTH)) ||
        !cJSON_IsString(source) ||
        !app_detection_source_parse(source->valuestring,
                                    &candidate->detection_source) ||
        !cJSON_IsString(profile) || !cJSON_IsString(algorithm) ||
        !cJSON_IsString(baseline) ||
        !parse_profile(profile->valuestring, &candidate->sensitivity_profile) ||
        !parse_algorithm(algorithm->valuestring,
                         &candidate->detector.algorithm) ||
        !parse_baseline(baseline->valuestring,
                        &candidate->detector.baseline_mode) ||
        !json_integer(sampling, "interval_ms", &interval) ||
        !json_integer(detector,
                      "window_size",
                      &candidate->detector.window_size) ||
        !json_integer(detector,
                      "calibration_samples",
                      &candidate->detector.calibration_samples) ||
        !json_float(detector,
                    "sigma_multiplier",
                    &candidate->detector.sigma_multiplier) ||
        !json_float(detector,
                    "minimum_threshold",
                    &candidate->detector.minimum_threshold) ||
        !json_float(detector,
                    "release_threshold_ratio",
                    &candidate->detector.release_threshold_ratio) ||
        !json_float(detector,
                    "baseline_update_alpha",
                    &candidate->detector.baseline_update_alpha) ||
        !json_integer(detector,
                      "trigger_consecutive",
                      &candidate->detector.trigger_consecutive) ||
        !json_integer(detector,
                      "clear_consecutive",
                      &candidate->detector.clear_consecutive)) {
        return false;
    }

    candidate->sample_interval_ms = (uint32_t)interval;
    strlcpy(candidate->wifi_ssid, ssid->valuestring, sizeof(candidate->wifi_ssid));
    if (password != NULL) {
        strlcpy(candidate->wifi_password,
                password->valuestring,
                sizeof(candidate->wifi_password));
    }
    return app_config_valid(candidate);
}

static char *receive_body(httpd_req_t *request)
{
    if (request->content_len == 0U || request->content_len > REQUEST_BODY_MAX) {
        return NULL;
    }
    char *body = malloc(request->content_len + 1U);
    if (body == NULL) {
        return NULL;
    }

    size_t received = 0U;
    while (received < request->content_len) {
        const int count = httpd_req_recv(request,
                                         body + received,
                                         request->content_len - received);
        if (count <= 0) {
            free(body);
            return NULL;
        }
        received += (size_t)count;
    }
    body[received] = '\0';
    return body;
}

static const char *json_required_string(const cJSON *object,
                                        const char *name)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

static esp_err_t session_handler(httpd_req_t *request)
{
    if (!request_authenticated(request)) {
        return require_authenticated(request);
    }
    cJSON *json = cJSON_CreateObject();
    if (json != NULL) {
        cJSON_AddBoolToObject(json, "authenticated", true);
        cJSON_AddStringToObject(json, "username", PORTAL_AUTH_USERNAME);
        cJSON_AddBoolToObject(json,
                              "default_password",
                              portal_auth_uses_default_password());
    }
    return send_json(request, json);
}

static esp_err_t login_handler(httpd_req_t *request)
{
    char *body = receive_body(request);
    cJSON *json = body != NULL ? cJSON_Parse(body) : NULL;
    free(body);
    const char *username = json_required_string(json, "username");
    const char *password = json_required_string(json, "password");
    char token[PORTAL_AUTH_SESSION_TOKEN_HEX_LENGTH + 1U];
    uint32_t retry_after_seconds = 0U;
    const portal_auth_login_result_t result = portal_auth_login(
        username,
        password,
        esp_timer_get_time() / 1000,
        token,
        &retry_after_seconds);
    cJSON_Delete(json);

    if (result == PORTAL_AUTH_LOGIN_LOCKED) {
        char retry_after[12];
        snprintf(retry_after, sizeof(retry_after), "%" PRIu32,
                 retry_after_seconds);
        httpd_resp_set_status(request, "429 Too Many Requests");
        httpd_resp_set_hdr(request, "Retry-After", retry_after);
        httpd_resp_set_hdr(request, "Cache-Control", "no-store");
        return httpd_resp_sendstr(request,
                                  "Demasiados intentos; espera antes de reintentar");
    }
    if (result != PORTAL_AUTH_LOGIN_OK) {
        httpd_resp_set_status(request, "401 Unauthorized");
        httpd_resp_set_hdr(request, "Cache-Control", "no-store");
        return httpd_resp_sendstr(request, "Usuario o contraseña incorrectos");
    }

    char cookie[160];
    snprintf(cookie,
             sizeof(cookie),
             SESSION_COOKIE_NAME "=%s; Path=/; HttpOnly; SameSite=Strict; "
             "Max-Age=1800",
             token);
    httpd_resp_set_hdr(request, "Set-Cookie", cookie);
    cJSON *response = cJSON_CreateObject();
    if (response != NULL) {
        cJSON_AddBoolToObject(response, "authenticated", true);
        cJSON_AddStringToObject(response, "username", PORTAL_AUTH_USERNAME);
        cJSON_AddBoolToObject(response,
                              "default_password",
                              portal_auth_uses_default_password());
    }
    return send_json(request, response);
}

static esp_err_t logout_handler(httpd_req_t *request)
{
    if (!request_authenticated(request)) {
        return require_authenticated(request);
    }
    portal_auth_logout();
    httpd_resp_set_hdr(request,
                       "Set-Cookie",
                       SESSION_COOKIE_NAME
                       "=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, "Sesión cerrada");
}

static esp_err_t change_password_handler(httpd_req_t *request)
{
    if (!request_authenticated(request)) {
        return require_authenticated(request);
    }
    char *body = receive_body(request);
    cJSON *json = body != NULL ? cJSON_Parse(body) : NULL;
    free(body);
    const char *current_password = json_required_string(json,
                                                        "current_password");
    const char *new_password = json_required_string(json, "new_password");
    const esp_err_t error = portal_auth_change_password(
        current_password,
        new_password,
        esp_timer_get_time() / 1000);
    cJSON_Delete(json);
    if (error == ESP_ERR_INVALID_ARG) {
        return httpd_resp_send_err(request,
                                   HTTPD_400_BAD_REQUEST,
                                   "La nueva contraseña debe tener entre 5 y 64 caracteres");
    }
    if (error == ESP_ERR_INVALID_CRC) {
        httpd_resp_set_status(request, "403 Forbidden");
        return httpd_resp_sendstr(request, "La contraseña actual no coincide");
    }
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Unable to change portal password: %s",
                 esp_err_to_name(error));
        return httpd_resp_send_err(request,
                                   HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "No se pudo guardar la contraseña");
    }
    httpd_resp_set_hdr(request,
                       "Set-Cookie",
                       SESSION_COOKIE_NAME
                       "=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request,
                              "Contraseña cambiada; inicia sesión de nuevo");
}

static const char *wifi_scan_state_name(wifi_scan_state_t state)
{
    switch (state) {
    case WIFI_SCAN_IDLE:
        return "idle";
    case WIFI_SCAN_RUNNING:
        return "scanning";
    case WIFI_SCAN_READY:
        return "ready";
    case WIFI_SCAN_ERROR:
        return "error";
    default:
        return "error";
    }
}

static const char *wifi_auth_name(wifi_auth_mode_t authmode)
{
    switch (authmode) {
    case WIFI_AUTH_OPEN:
        return "Abierta";
    case WIFI_AUTH_WEP:
        return "WEP";
    case WIFI_AUTH_WPA_PSK:
        return "WPA";
    case WIFI_AUTH_WPA2_PSK:
        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:
        return "WPA/WPA2";
    case WIFI_AUTH_WPA3_PSK:
        return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:
        return "WPA2/WPA3";
    case WIFI_AUTH_WAPI_PSK:
        return "WAPI";
    case WIFI_AUTH_OWE:
        return "OWE";
    case WIFI_AUTH_DPP:
        return "DPP";
    case WIFI_AUTH_ENTERPRISE:
    case WIFI_AUTH_WPA3_ENT_192:
    case WIFI_AUTH_WPA3_ENTERPRISE:
    case WIFI_AUTH_WPA2_WPA3_ENTERPRISE:
    case WIFI_AUTH_WPA_ENTERPRISE:
        return "Enterprise";
    default:
        return "Desconocida";
    }
}

static bool wifi_auth_supported(wifi_auth_mode_t authmode)
{
    return authmode == WIFI_AUTH_OPEN || authmode == WIFI_AUTH_WPA2_PSK ||
           authmode == WIFI_AUTH_WPA_WPA2_PSK ||
           authmode == WIFI_AUTH_WPA3_PSK ||
           authmode == WIFI_AUTH_WPA2_WPA3_PSK;
}

static bool wifi_auth_requires_password(wifi_auth_mode_t authmode)
{
    return wifi_auth_supported(authmode) && authmode != WIFI_AUTH_OPEN;
}

static void wifi_scan_event_handler(void *argument,
                                    esp_event_base_t event_base,
                                    int32_t event_id,
                                    void *event_data)
{
    (void)argument;
    if (event_base != WIFI_EVENT || event_id != WIFI_EVENT_SCAN_DONE ||
        wifi_scan_mutex == NULL) {
        return;
    }

    const wifi_event_sta_scan_done_t *event = event_data;
    uint16_t raw_count = WIFI_SCAN_MAX_RESULTS;
    esp_err_t error = event != NULL && event->status == 0U
        ? esp_wifi_scan_get_ap_records(&raw_count, wifi_scan_raw_results)
        : ESP_FAIL;
    const int64_t now_ms = esp_timer_get_time() / 1000;

    if (xSemaphoreTake(wifi_scan_mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        atomic_store(&wifi_scan_detector_guard_until_ms,
                     now_ms + WIFI_SCAN_DETECTOR_GUARD_MS);
        return;
    }
    wifi_scan_result_count = 0U;
    if (error == ESP_OK) {
        for (uint16_t raw_index = 0U;
             raw_index < raw_count &&
             wifi_scan_result_count < WIFI_SCAN_MAX_RESULTS;
             ++raw_index) {
            const wifi_ap_record_t *raw = &wifi_scan_raw_results[raw_index];
            const size_t ssid_length = strnlen((const char *)raw->ssid,
                                               sizeof(raw->ssid));
            if (ssid_length == 0U) {
                continue;
            }
            size_t result_index = 0U;
            for (; result_index < wifi_scan_result_count; ++result_index) {
                if (strncmp(wifi_scan_results[result_index].ssid,
                            (const char *)raw->ssid,
                            sizeof(wifi_scan_results[result_index].ssid)) == 0) {
                    break;
                }
            }
            if (result_index < wifi_scan_result_count) {
                if (raw->rssi > wifi_scan_results[result_index].rssi) {
                    wifi_scan_results[result_index].rssi = raw->rssi;
                    wifi_scan_results[result_index].channel = raw->primary;
                    wifi_scan_results[result_index].authmode = raw->authmode;
                }
                continue;
            }
            wifi_scan_result_t *result =
                &wifi_scan_results[wifi_scan_result_count++];
            memcpy(result->ssid, raw->ssid, ssid_length);
            result->ssid[ssid_length] = '\0';
            result->rssi = raw->rssi;
            result->channel = raw->primary;
            result->authmode = raw->authmode;
        }
        wifi_scan_state = WIFI_SCAN_READY;
        wifi_scan_last_error = ESP_OK;
        ESP_LOGI(TAG, "WiFi scan completed with %u selectable SSIDs",
                 (unsigned)wifi_scan_result_count);
    } else {
        wifi_scan_state = WIFI_SCAN_ERROR;
        wifi_scan_last_error = error;
        ESP_LOGW(TAG, "WiFi scan failed: %s", esp_err_to_name(error));
    }
    wifi_scan_updated_ms = now_ms;
    xSemaphoreGive(wifi_scan_mutex);
    atomic_store(&wifi_scan_detector_guard_until_ms,
                 now_ms + WIFI_SCAN_DETECTOR_GUARD_MS);
}

static esp_err_t start_wifi_scan_handler(httpd_req_t *request)
{
    if (!request_authenticated(request)) {
        return require_authenticated(request);
    }
    if (xSemaphoreTake(wifi_scan_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        httpd_resp_set_status(request, "503 Service Unavailable");
        return httpd_resp_sendstr(request, "wifi scan busy");
    }
    if (wifi_scan_state == WIFI_SCAN_RUNNING) {
        xSemaphoreGive(wifi_scan_mutex);
        httpd_resp_set_status(request, "409 Conflict");
        return httpd_resp_sendstr(request, "Ya hay un escaneo en curso");
    }
    wifi_scan_state = WIFI_SCAN_RUNNING;
    wifi_scan_started_ms = esp_timer_get_time() / 1000;
    wifi_scan_updated_ms = wifi_scan_started_ms;
    wifi_scan_last_error = ESP_OK;
    xSemaphoreGive(wifi_scan_mutex);
    atomic_store(&wifi_scan_detector_guard_until_ms, INT64_MAX);

    const esp_err_t error = esp_wifi_scan_start(NULL, false);
    if (error != ESP_OK) {
        if (xSemaphoreTake(wifi_scan_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            wifi_scan_state = WIFI_SCAN_ERROR;
            wifi_scan_last_error = error;
            wifi_scan_updated_ms = esp_timer_get_time() / 1000;
            xSemaphoreGive(wifi_scan_mutex);
        }
        atomic_store(&wifi_scan_detector_guard_until_ms, 0);
        ESP_LOGW(TAG, "Unable to start WiFi scan: %s",
                 esp_err_to_name(error));
        return httpd_resp_send_err(request,
                                   HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "No se pudo iniciar el escaneo Wi-Fi");
    }
    httpd_resp_set_status(request, "202 Accepted");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, "Escaneo Wi-Fi iniciado");
}

static esp_err_t get_wifi_scan_handler(httpd_req_t *request)
{
    if (!request_authenticated(request)) {
        return require_authenticated(request);
    }
    const int64_t now_ms = esp_timer_get_time() / 1000;
    if (xSemaphoreTake(wifi_scan_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        httpd_resp_set_status(request, "503 Service Unavailable");
        return httpd_resp_sendstr(request, "wifi scan busy");
    }
    if (wifi_scan_state == WIFI_SCAN_RUNNING &&
        now_ms - wifi_scan_started_ms > WIFI_SCAN_TIMEOUT_MS) {
        wifi_scan_state = WIFI_SCAN_ERROR;
        wifi_scan_last_error = ESP_ERR_TIMEOUT;
        wifi_scan_updated_ms = now_ms;
        atomic_store(&wifi_scan_detector_guard_until_ms, 0);
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *networks = root != NULL
        ? cJSON_AddArrayToObject(root, "networks")
        : NULL;
    if (root == NULL || networks == NULL) {
        cJSON_Delete(root);
        xSemaphoreGive(wifi_scan_mutex);
        return httpd_resp_send_err(request,
                                   HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "out of memory");
    }
    cJSON_AddStringToObject(root, "state",
                            wifi_scan_state_name(wifi_scan_state));
    cJSON_AddNumberToObject(root, "updated_ms", (double)wifi_scan_updated_ms);
    cJSON_AddBoolToObject(root,
                          "sampling_paused",
                          config_portal_wifi_scan_active(now_ms));
    if (wifi_scan_state == WIFI_SCAN_ERROR) {
        cJSON_AddStringToObject(root,
                                "error",
                                esp_err_to_name(wifi_scan_last_error));
    }
    for (size_t index = 0U; index < wifi_scan_result_count; ++index) {
        const wifi_scan_result_t *result = &wifi_scan_results[index];
        cJSON *network = cJSON_CreateObject();
        if (network == NULL) {
            cJSON_Delete(root);
            xSemaphoreGive(wifi_scan_mutex);
            return httpd_resp_send_err(request,
                                       HTTPD_500_INTERNAL_SERVER_ERROR,
                                       "out of memory");
        }
        cJSON_AddStringToObject(network, "ssid", result->ssid);
        cJSON_AddNumberToObject(network, "rssi", result->rssi);
        cJSON_AddNumberToObject(network, "channel", result->channel);
        cJSON_AddStringToObject(network,
                                "security",
                                wifi_auth_name(result->authmode));
        cJSON_AddBoolToObject(network,
                              "password_required",
                              wifi_auth_requires_password(result->authmode));
        cJSON_AddBoolToObject(network,
                              "supported",
                              wifi_auth_supported(result->authmode));
        cJSON_AddBoolToObject(network,
                              "selected",
                              strcmp(result->ssid,
                                     current_config.wifi_ssid) == 0);
        cJSON_AddItemToArray(networks, network);
    }
    xSemaphoreGive(wifi_scan_mutex);
    return send_json(request, root);
}

static cJSON *telegram_json(void)
{
    telegram_notifier_snapshot_t snapshot;
    telegram_notifier_get_snapshot(&snapshot);
    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        return NULL;
    }
    cJSON_AddBoolToObject(json, "enabled", snapshot.enabled);
    cJSON_AddBoolToObject(json, "token_set", snapshot.token_set);
    cJSON_AddStringToObject(json, "chat_id", snapshot.chat_id);
    cJSON_AddBoolToObject(json, "busy", snapshot.busy);
    cJSON_AddNumberToObject(json, "queue_depth", snapshot.queue_depth);
    cJSON_AddNumberToObject(json, "sent", (double)snapshot.sent);
    cJSON_AddNumberToObject(json, "failures", (double)snapshot.failures);
    cJSON_AddNumberToObject(json, "dropped", (double)snapshot.dropped);
    cJSON_AddNumberToObject(json, "last_http_status", snapshot.last_http_status);
    cJSON_AddStringToObject(json,
                            "last_error",
                            esp_err_to_name(snapshot.last_error));
    return json;
}

static esp_err_t get_telegram_handler(httpd_req_t *request)
{
    if (!request_authenticated(request)) {
        return require_authenticated(request);
    }
    return send_json(request, telegram_json());
}

static esp_err_t update_telegram_handler(httpd_req_t *request)
{
    if (!request_authenticated(request)) {
        return require_authenticated(request);
    }
    char *body = receive_body(request);
    cJSON *json = body != NULL ? cJSON_Parse(body) : NULL;
    free(body);
    const cJSON *enabled = json != NULL
        ? cJSON_GetObjectItemCaseSensitive(json, "enabled")
        : NULL;
    const cJSON *token = json != NULL
        ? cJSON_GetObjectItemCaseSensitive(json, "token")
        : NULL;
    const cJSON *clear_token = json != NULL
        ? cJSON_GetObjectItemCaseSensitive(json, "clear_token")
        : NULL;
    const cJSON *chat_id = json != NULL
        ? cJSON_GetObjectItemCaseSensitive(json, "chat_id")
        : NULL;
    if (!cJSON_IsBool(enabled) || !cJSON_IsString(chat_id) ||
        (token != NULL && !cJSON_IsString(token)) ||
        (clear_token != NULL && !cJSON_IsBool(clear_token))) {
        cJSON_Delete(json);
        return httpd_resp_send_err(request,
                                   HTTPD_400_BAD_REQUEST,
                                   "Configuración de Telegram inválida");
    }
    const esp_err_t error = telegram_notifier_update(
        cJSON_IsTrue(enabled),
        token != NULL ? token->valuestring : NULL,
        clear_token != NULL && cJSON_IsTrue(clear_token),
        chat_id->valuestring);
    cJSON_Delete(json);
    if (error == ESP_ERR_INVALID_ARG) {
        return httpd_resp_send_err(request,
                                   HTTPD_400_BAD_REQUEST,
                                   "Token o chat_id inválido; Telegram habilitado requiere ambos");
    }
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Unable to save Telegram configuration: %s",
                 esp_err_to_name(error));
        return httpd_resp_send_err(request,
                                   HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "No se pudo guardar Telegram");
    }
    return send_json(request, telegram_json());
}

static esp_err_t test_telegram_handler(httpd_req_t *request)
{
    if (!request_authenticated(request)) {
        return require_authenticated(request);
    }
    const esp_err_t error = telegram_notifier_enqueue_test();
    if (error == ESP_ERR_INVALID_STATE) {
        return httpd_resp_send_err(request,
                                   HTTPD_400_BAD_REQUEST,
                                   "Configura token y chat_id antes de probar");
    }
    if (error != ESP_OK) {
        httpd_resp_set_status(request, "503 Service Unavailable");
        return httpd_resp_sendstr(request, "La cola de Telegram está llena");
    }
    httpd_resp_set_status(request, "202 Accepted");
    return httpd_resp_sendstr(request, "Mensaje de prueba encolado");
}

static void restart_task(void *argument)
{
    (void)argument;
    vTaskDelay(pdMS_TO_TICKS(750));
    esp_restart();
}

static void schedule_restart(void)
{
    if (xTaskCreate(restart_task, "config_restart", 2048, NULL, 5, NULL) !=
        pdPASS) {
        ESP_LOGE(TAG, "Unable to schedule restart");
    }
}

static esp_err_t update_config_handler(httpd_req_t *request)
{
    if (!request_authenticated(request)) {
        return require_authenticated(request);
    }
    httpd_resp_set_hdr(request, "Connection", "close");
    char *body = receive_body(request);
    cJSON *json = body != NULL ? cJSON_Parse(body) : NULL;
    free(body);
    app_runtime_config_t candidate = current_config;
    if (json == NULL || !parse_config_json(json, &candidate)) {
        cJSON_Delete(json);
        return httpd_resp_send_err(request,
                                   HTTPD_400_BAD_REQUEST,
                                   "invalid configuration");
    }
    cJSON_Delete(json);

    const esp_err_t error = app_config_save(&candidate);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Unable to save configuration: %s", esp_err_to_name(error));
        return httpd_resp_send_err(request,
                                   HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "unable to save configuration");
    }
    current_config = candidate;
    httpd_resp_set_status(request, "202 Accepted");
    const esp_err_t response = httpd_resp_sendstr(request,
                                                   "Guardado; reiniciando...");
    schedule_restart();
    return response;
}

static esp_err_t factory_reset_handler(httpd_req_t *request)
{
    if (!request_authenticated(request)) {
        return require_authenticated(request);
    }
    httpd_resp_set_hdr(request, "Connection", "close");
    esp_err_t error = app_config_erase();
    if (error == ESP_OK) {
        error = portal_auth_erase();
    }
    if (error == ESP_OK) {
        error = telegram_notifier_erase();
    }
    if (error == ESP_OK) {
        error = esp_wifi_restore();
    }
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Factory reset failed: %s", esp_err_to_name(error));
        return httpd_resp_send_err(request,
                                   HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "factory reset failed");
    }
    httpd_resp_set_status(request, "202 Accepted");
    const esp_err_t response = httpd_resp_sendstr(request,
                                                   "Restaurado; reiniciando...");
    schedule_restart();
    return response;
}

static esp_err_t calibrate_handler(httpd_req_t *request)
{
    if (!request_authenticated(request)) {
        return require_authenticated(request);
    }
    httpd_resp_set_hdr(request, "Connection", "close");
    char *body = receive_body(request);
    cJSON *json = body != NULL ? cJSON_Parse(body) : NULL;
    free(body);
    const cJSON *delay = json != NULL
        ? cJSON_GetObjectItemCaseSensitive(json, "delay_seconds")
        : NULL;
    if (!cJSON_IsNumber(delay) || delay->valuedouble != (double)delay->valueint ||
        delay->valueint < CALIBRATION_DELAY_MIN_SECONDS ||
        delay->valueint > CALIBRATION_DELAY_MAX_SECONDS) {
        cJSON_Delete(json);
        return httpd_resp_send_err(request,
                                   HTTPD_400_BAD_REQUEST,
                                   "delay_seconds must be an integer from 5 to 120");
    }
    const int delay_seconds = delay->valueint;
    cJSON_Delete(json);

    const int64_t deadline_ms = esp_timer_get_time() / 1000 +
                                (int64_t)delay_seconds * 1000;
    atomic_store(&calibration_deadline_ms, deadline_ms);
    ESP_LOGI(TAG,
             "Detector recalibration scheduled in %d seconds",
             delay_seconds);
    httpd_resp_set_status(request, "202 Accepted");
    return httpd_resp_sendstr(request, "Recalibración programada");
}

static motion_detector_state_t selected_detector_state(
    const config_portal_diagnostics_t *snapshot)
{
    if (current_config.detection_source == APP_DETECTION_SOURCE_RSSI) {
        return snapshot->detector.state;
    }
    if (current_config.detection_source == APP_DETECTION_SOURCE_CSI) {
        return snapshot->csi_detector.state;
    }
    if (snapshot->detector.state == MOTION_DETECTOR_ACTIVE ||
        snapshot->csi_detector.state == MOTION_DETECTOR_ACTIVE) {
        return MOTION_DETECTOR_ACTIVE;
    }
    if (!snapshot->detector.calibrated ||
        !snapshot->csi_detector.calibrated) {
        return MOTION_DETECTOR_CALIBRATING;
    }
    return MOTION_DETECTOR_IDLE;
}

static esp_err_t diagnostics_handler(httpd_req_t *request)
{
    if (!request_authenticated(request)) {
        return require_authenticated(request);
    }
    config_portal_diagnostics_t snapshot = {0};
    if (xSemaphoreTake(diagnostics_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        snapshot = current_diagnostics;
        xSemaphoreGive(diagnostics_mutex);
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *config = config_json(&current_config, false);
    cJSON *runtime = cJSON_AddObjectToObject(root, "runtime");
    if (root == NULL || config == NULL || runtime == NULL) {
        cJSON_Delete(config);
        cJSON_Delete(root);
        return httpd_resp_send_err(request,
                                   HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "out of memory");
    }
    cJSON_AddItemToObject(root, "config", config);
    cJSON_AddNumberToObject(runtime, "uptime_ms", (double)snapshot.uptime_ms);
    cJSON_AddBoolToObject(runtime, "sample_ok", snapshot.sample_ok);
    cJSON_AddNumberToObject(runtime, "rssi_dbm", snapshot.rssi_dbm);
    cJSON_AddNumberToObject(runtime, "channel", snapshot.channel);
    cJSON_AddStringToObject(runtime, "bssid", snapshot.bssid);
    cJSON_AddStringToObject(
        runtime,
        "detection_source",
        app_detection_source_name(current_config.detection_source));
    cJSON_AddStringToObject(
        runtime,
        "selected_state",
        motion_detector_state_name(selected_detector_state(&snapshot)));
    cJSON_AddStringToObject(runtime,
                            "state",
                            motion_detector_state_name(snapshot.detector.state));
    cJSON_AddBoolToObject(runtime, "calibrated", snapshot.detector.calibrated);
    cJSON_AddNumberToObject(runtime, "score", snapshot.detector.score);
    cJSON_AddNumberToObject(runtime, "threshold", snapshot.detector.threshold);
    cJSON_AddNumberToObject(runtime,
                            "release_threshold",
                            snapshot.detector.release_threshold);
    cJSON *csi_detector = cJSON_AddObjectToObject(runtime, "csi_detector");
    if (csi_detector == NULL) {
        cJSON_Delete(root);
        return httpd_resp_send_err(request,
                                   HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "out of memory");
    }
    cJSON_AddStringToObject(
        csi_detector,
        "state",
        motion_detector_state_name(snapshot.csi_detector.state));
    cJSON_AddBoolToObject(csi_detector,
                          "calibrated",
                          snapshot.csi_detector.calibrated);
    cJSON_AddNumberToObject(csi_detector,
                            "score",
                            snapshot.csi_detector.score);
    cJSON_AddNumberToObject(csi_detector,
                            "threshold",
                            snapshot.csi_detector.threshold);
    cJSON_AddNumberToObject(runtime, "queries", (double)snapshot.queries);
    cJSON_AddNumberToObject(runtime, "samples_ok", (double)snapshot.samples_ok);
    cJSON_AddNumberToObject(runtime, "read_errors", (double)snapshot.read_errors);
    cJSON_AddNumberToObject(runtime,
                            "schedule_misses",
                            (double)snapshot.schedule_misses);
    cJSON_AddNumberToObject(runtime, "disconnects", (double)snapshot.disconnects);
    cJSON_AddNumberToObject(runtime, "reconnects", (double)snapshot.reconnects);
    const int64_t now_ms = esp_timer_get_time() / 1000;
    const int64_t calibration_deadline =
        atomic_load(&calibration_deadline_ms);
    const int64_t calibration_remaining_ms = calibration_deadline > now_ms
        ? calibration_deadline - now_ms
        : 0;
    cJSON_AddBoolToObject(runtime,
                          "calibration_pending",
                          calibration_deadline != 0);
    cJSON_AddNumberToObject(runtime,
                            "calibration_remaining_ms",
                            (double)calibration_remaining_ms);
    return send_json(request, root);
}

esp_err_t config_portal_start(const app_runtime_config_t *effective_config,
                              bool provisioning_mode)
{
    if (!app_config_valid(effective_config)) {
        return ESP_ERR_INVALID_ARG;
    }
    current_config = *effective_config;
    current_provisioning_mode = provisioning_mode;
    atomic_store(&calibration_deadline_ms, 0);
    atomic_store(&wifi_scan_detector_guard_until_ms, 0);
    esp_err_t error = portal_auth_init();
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Unable to initialize portal authentication: %s",
                 esp_err_to_name(error));
        return error;
    }
    diagnostics_mutex = xSemaphoreCreateMutex();
    wifi_scan_mutex = xSemaphoreCreateMutex();
    if (diagnostics_mutex == NULL || wifi_scan_mutex == NULL) {
        if (diagnostics_mutex != NULL) {
            vSemaphoreDelete(diagnostics_mutex);
            diagnostics_mutex = NULL;
        }
        if (wifi_scan_mutex != NULL) {
            vSemaphoreDelete(wifi_scan_mutex);
            wifi_scan_mutex = NULL;
        }
        return ESP_ERR_NO_MEM;
    }
    wifi_scan_state = WIFI_SCAN_IDLE;
    wifi_scan_started_ms = 0;
    wifi_scan_updated_ms = 0;
    wifi_scan_last_error = ESP_OK;
    wifi_scan_result_count = 0U;
    error = esp_event_handler_instance_register(
        WIFI_EVENT,
        WIFI_EVENT_SCAN_DONE,
        &wifi_scan_event_handler,
        NULL,
        &wifi_scan_event_instance);
    if (error != ESP_OK) {
        vSemaphoreDelete(diagnostics_mutex);
        vSemaphoreDelete(wifi_scan_mutex);
        diagnostics_mutex = NULL;
        wifi_scan_mutex = NULL;
        return error;
    }

    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
    server_config.max_uri_handlers = 18;
    server_config.lru_purge_enable = true;
    server_config.uri_match_fn = httpd_uri_match_wildcard;
    httpd_handle_t server = NULL;
    error = httpd_start(&server, &server_config);
    if (error != ESP_OK) {
        esp_event_handler_instance_unregister(WIFI_EVENT,
                                              WIFI_EVENT_SCAN_DONE,
                                              wifi_scan_event_instance);
        vSemaphoreDelete(diagnostics_mutex);
        vSemaphoreDelete(wifi_scan_mutex);
        diagnostics_mutex = NULL;
        wifi_scan_mutex = NULL;
        return error;
    }

    const httpd_uri_t handlers[] = {
        {.uri = "/", .method = HTTP_GET, .handler = index_handler},
        {.uri = "/api/session", .method = HTTP_GET, .handler = session_handler},
        {.uri = "/api/login", .method = HTTP_POST, .handler = login_handler},
        {.uri = "/api/logout", .method = HTTP_POST, .handler = logout_handler},
        {.uri = "/api/password", .method = HTTP_POST, .handler = change_password_handler},
        {.uri = "/api/wifi/scan", .method = HTTP_POST, .handler = start_wifi_scan_handler},
        {.uri = "/api/wifi/scan", .method = HTTP_GET, .handler = get_wifi_scan_handler},
        {.uri = "/api/telegram", .method = HTTP_GET, .handler = get_telegram_handler},
        {.uri = "/api/telegram", .method = HTTP_POST, .handler = update_telegram_handler},
        {.uri = "/api/telegram/test", .method = HTTP_POST, .handler = test_telegram_handler},
        {.uri = "/api/config", .method = HTTP_GET, .handler = get_config_handler},
        {.uri = "/api/config", .method = HTTP_POST, .handler = update_config_handler},
        {.uri = "/api/export", .method = HTTP_GET, .handler = export_handler},
        {.uri = "/api/import", .method = HTTP_POST, .handler = update_config_handler},
        {.uri = "/api/factory-reset", .method = HTTP_POST, .handler = factory_reset_handler},
        {.uri = "/api/calibrate", .method = HTTP_POST, .handler = calibrate_handler},
        {.uri = "/api/diagnostics", .method = HTTP_GET, .handler = diagnostics_handler},
        {.uri = "/*", .method = HTTP_GET, .handler = captive_redirect_handler},
    };
    for (size_t index = 0U; index < sizeof(handlers) / sizeof(handlers[0]);
         ++index) {
        error = httpd_register_uri_handler(server, &handlers[index]);
        if (error != ESP_OK) {
            httpd_stop(server);
            esp_event_handler_instance_unregister(WIFI_EVENT,
                                                  WIFI_EVENT_SCAN_DONE,
                                                  wifi_scan_event_instance);
            vSemaphoreDelete(diagnostics_mutex);
            vSemaphoreDelete(wifi_scan_mutex);
            diagnostics_mutex = NULL;
            wifi_scan_mutex = NULL;
            return error;
        }
    }
    ESP_LOGI(TAG, "Configuration portal ready on port 80");
    return ESP_OK;
}

bool config_portal_take_calibration_request(int64_t timestamp_ms)
{
    int64_t deadline = atomic_load(&calibration_deadline_ms);
    if (deadline == 0 || timestamp_ms < deadline) {
        return false;
    }
    return atomic_compare_exchange_strong(&calibration_deadline_ms,
                                           &deadline,
                                           0);
}

bool config_portal_wifi_scan_active(int64_t timestamp_ms)
{
    return timestamp_ms < atomic_load(&wifi_scan_detector_guard_until_ms);
}

void config_portal_update_diagnostics(
    const config_portal_diagnostics_t *diagnostics)
{
    if (diagnostics == NULL || diagnostics_mutex == NULL) {
        return;
    }
    if (xSemaphoreTake(diagnostics_mutex, 0) == pdTRUE) {
        current_diagnostics = *diagnostics;
        xSemaphoreGive(diagnostics_mutex);
    }
}
