#pragma once

#define IDR_MAINMENU 101

#define IDM_FILE_EXIT 40001
#define IDM_FILE_EXPORT_PRESET 40002
#define IDM_FILE_IMPORT_PRESET 40003
#define IDM_SETTINGS_OPEN 40004

#define IDC_STATUS_INPUT 50001
#define IDC_STATUS_OUTPUT 50002

#define IDC_POWER_TOGGLE  50100
#define IDC_GAIN_SLIDER   50101
#define IDC_RECORD_TOGGLE 50102
#define IDC_FORMAT_COMBO  50103

// Per-slot controls: base + slotIndex (0-3)
#define IDC_SLOT_ADD_BASE    51000
#define IDC_SLOT_BYPASS_BASE 51010
#define IDC_SLOT_DELETE_BASE 51020

#define IDC_PLUGIN_LIST 51100

// Per-slot dynamic parameter controls:
//   slot s, port index p  ->  IDC_PARAM_BASE + s * 500 + p
#define IDC_PARAM_BASE 52000

#define IDT_ENGINE_STATE 1
