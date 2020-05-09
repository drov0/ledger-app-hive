/*******************************************************************************
*   Taras Shchybovyk
*   (c) 2020 Andrew Chaney
*
*  Licensed under the Apache License, Version 2.0 (the "License");
*  you may not use this file except in compliance with the License.
*  You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
*  Unless required by applicable law or agreed to in writing, software
*  distributed under the License is distributed on an "AS IS" BASIS,
*  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*  See the License for the specific language governing permissions and
*  limitations under the License.
********************************************************************************/

#include "os.h"
#include "cx.h"
#include <stdbool.h>

#include "os_io_seproxyhal.h"
#include "string.h"
#include "hive_utils.h"
#include "hive_stream.h"

#include "glyphs.h"

unsigned char G_io_seproxyhal_spi_buffer[IO_SEPROXYHAL_BUFFER_SIZE_B];

unsigned int io_seproxyhal_touch_settings(const bagl_element_t *e);
unsigned int io_seproxyhal_touch_exit(const bagl_element_t *e);
unsigned int io_seproxyhal_touch_tx_ok(const bagl_element_t *e);
unsigned int io_seproxyhal_touch_tx_cancel(const bagl_element_t *e);
unsigned int io_seproxyhal_touch_address_ok(const bagl_element_t *e);
unsigned int io_seproxyhal_touch_address_cancel(const bagl_element_t *e);
void io_exchange_with_code(uint16_t code, uint32_t tx);
void ui_idle(void);

uint32_t get_public_key_and_set_result(void);
uint32_t sign_hash_and_set_result(void);

#if defined(TARGET_NANOS)
unsigned int ui_address_nanos_button(unsigned int button_mask, unsigned int button_mask_counter);
unsigned int ui_single_action_tx_approval_nanos_button(unsigned int button_mask, unsigned int button_mask_counter);
unsigned int ui_multiple_action_tx_approval_nanos_button(unsigned int button_mask, unsigned int button_mask_counter);
#endif // #if defined(TARGET_NANOS)

#define MAX_BIP32_PATH 10

#define CLA 0xD4
#define INS_GET_PUBLIC_KEY 0x02
#define INS_SIGN 0x04
#define INS_GET_APP_CONFIGURATION 0x06
#define P1_CONFIRM 0x01
#define P1_NON_CONFIRM 0x00
#define P2_NO_CHAINCODE 0x00
#define P2_CHAINCODE 0x01
#define P1_FIRST 0x00
#define P1_MORE 0x80

#define OFFSET_CLA 0
#define OFFSET_INS 1
#define OFFSET_P1 2
#define OFFSET_P2 3
#define OFFSET_LC 4
#define OFFSET_CDATA 5

uint8_t const SECP256K1_N[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                               0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe,
                               0xba, 0xae, 0xdc, 0xe6, 0xaf, 0x48, 0xa0, 0x3b,
                               0xbf, 0xd2, 0x5e, 0x8c, 0xd0, 0x36, 0x41, 0x41};

typedef struct publicKeyContext_t
{
    cx_ecfp_public_key_t publicKey;
    char address[60];
    uint8_t chainCode[32];
    bool getChaincode;
} publicKeyContext_t;

typedef struct transactionContext_t
{
    uint8_t pathLength;
    uint32_t bip32Path[MAX_BIP32_PATH];
    uint8_t hash[32];
} transactionContext_t;

cx_sha256_t sha256;
cx_sha256_t dataSha256;

union {
    publicKeyContext_t publicKeyContext;
    transactionContext_t transactionContext;
} tmpCtx;

txProcessingContext_t txProcessingCtx;
txProcessingContent_t txContent;

volatile char actionCounter[32];
volatile char confirmLabel[32];

#ifdef TARGET_NANOX

#include "ux.h"
ux_state_t G_ux;
bolos_ux_params_t G_ux_params;

#else // TARGET_NANOX

ux_state_t ux;

#endif // TARGET_NANOX
// display stepped screens
unsigned int ux_step;
unsigned int ux_step_count;

typedef struct internalStorage_t {
    uint8_t dataAllowed;
    uint8_t initialized;
} internalStorage_t;

const internalStorage_t N_storage_real;
#define N_storage (*(volatile internalStorage_t *)PIC(&N_storage_real))

const bagl_element_t *ui_menu_item_out_over(const bagl_element_t *e)
{
    // the selection rectangle is after the none|touchable
    e = (const bagl_element_t *)(((unsigned int)e) + sizeof(bagl_element_t));
    return e;
}

#if defined(TARGET_NANOS)

const ux_menu_entry_t menu_main[];
const ux_menu_entry_t menu_settings[];
const ux_menu_entry_t menu_settings_data[];

#ifdef HAVE_U2F

// change the setting
void menu_settings_data_change(unsigned int enabled)
{
    uint8_t dataAllowed = enabled;
    nvm_write(&N_storage.dataAllowed, (void *)&dataAllowed, sizeof(uint8_t));
    // go back to the menu entry
    UX_MENU_DISPLAY(0, menu_settings, NULL);
}

// show the currently activated entry
void menu_settings_data_init(unsigned int ignored) {
  UNUSED(ignored);
  UX_MENU_DISPLAY(N_storage.dataAllowed?1:0, menu_settings_data, NULL);
}

const ux_menu_entry_t menu_settings_data[] = {
    {NULL, menu_settings_data_change, 0, NULL, "No", NULL, 0, 0},
    {NULL, menu_settings_data_change, 1, NULL, "Yes", NULL, 0, 0},
    UX_MENU_END};

const ux_menu_entry_t menu_settings[] = {
    {NULL, menu_settings_data_init, 0, NULL, "Arbitrary data", NULL, 0, 0},
    {menu_main, NULL, 1, &C_icon_back, "Back", NULL, 61, 40},
    UX_MENU_END};
#endif // HAVE_U2F

const ux_menu_entry_t menu_about[] = {
    {NULL, NULL, 0, NULL, "Version", APPVERSION, 0, 0},
    {menu_main, NULL, 2, &C_icon_back, "Back", NULL, 61, 40},
    UX_MENU_END};

const ux_menu_entry_t menu_main[] = {
    {NULL, NULL, 0, &C_nanos_badge_hive, "Use wallet to",
     "view accounts", 33, 12},
    {menu_settings, NULL, 0, NULL, "Settings", NULL, 0, 0},
    {menu_about, NULL, 0, NULL, "About", NULL, 0, 0},
    {NULL, os_sched_exit, 0, &C_icon_dashboard, "Quit app", NULL, 50, 29},
    UX_MENU_END};

const bagl_element_t ui_address_nanos[] = {
    // type                               userid    x    y   w    h  str rad
    // fill      fg        bg      fid iid  txt   touchparams...       ]
    {{BAGL_RECTANGLE, 0x00, 0, 0, 128, 32, 0, 0, BAGL_FILL, 0x000000, 0xFFFFFF,
      0, 0},
     NULL,
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},

    {{BAGL_ICON, 0x00, 3, 12, 7, 7, 0, 0, 0, 0xFFFFFF, 0x000000, 0,
      BAGL_GLYPH_ICON_CROSS},
     NULL,
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},
    {{BAGL_ICON, 0x00, 117, 13, 8, 6, 0, 0, 0, 0xFFFFFF, 0x000000, 0,
      BAGL_GLYPH_ICON_CHECK},
     NULL,
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},

    //{{BAGL_ICON                           , 0x01,  31,   9,  14,  14, 0, 0, 0
    //, 0xFFFFFF, 0x000000, 0, BAGL_GLYPH_ICON_EYE_BADGE  }, NULL, 0, 0, 0,
    //NULL, NULL, NULL },
    {{BAGL_LABELINE, 0x01, 0, 12, 128, 12, 0, 0, 0, 0xFFFFFF, 0x000000,
      BAGL_FONT_OPEN_SANS_EXTRABOLD_11px | BAGL_FONT_ALIGNMENT_CENTER, 0},
     "Confirm",
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},
    {{BAGL_LABELINE, 0x01, 0, 26, 128, 12, 0, 0, 0, 0xFFFFFF, 0x000000,
      BAGL_FONT_OPEN_SANS_EXTRABOLD_11px | BAGL_FONT_ALIGNMENT_CENTER, 0},
     "Public Key",
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},

    {{BAGL_LABELINE, 0x02, 0, 12, 128, 12, 0, 0, 0, 0xFFFFFF, 0x000000,
      BAGL_FONT_OPEN_SANS_REGULAR_11px | BAGL_FONT_ALIGNMENT_CENTER, 0},
     "Public Key",
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},
    {{BAGL_LABELINE, 0x02, 23, 26, 82, 12, 0x80 | 10, 0, 0, 0xFFFFFF, 0x000000,
      BAGL_FONT_OPEN_SANS_EXTRABOLD_11px | BAGL_FONT_ALIGNMENT_CENTER, 26},
     (char *)tmpCtx.publicKeyContext.address,
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},
};

unsigned int ui_address_prepro(const bagl_element_t *element)
{
    if (element->component.userid > 0)
    {
        unsigned int display = (ux_step == element->component.userid - 1);
        if (display)
        {
            switch (element->component.userid)
            {
            case 1:
                UX_CALLBACK_SET_INTERVAL(2000);
                break;
            case 2:
                UX_CALLBACK_SET_INTERVAL(MAX(
                    3000, 1000 + bagl_label_roundtrip_duration_ms(element, 7)));
                break;
            }
        }
        return display;
    }
    return 1;
}

const bagl_element_t ui_single_action_tx_approval_nanos[] = {
    // type                               userid    x    y   w    h  str rad
    // fill      fg        bg      fid iid  txt   touchparams...       ]
    {{BAGL_RECTANGLE, 0x00, 0, 0, 128, 32, 0, 0, BAGL_FILL, 0x000000, 0xFFFFFF,
      0, 0},
     NULL,
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},

    {{BAGL_ICON, 0x00, 3, 12, 7, 7, 0, 0, 0, 0xFFFFFF, 0x000000, 0,
      BAGL_GLYPH_ICON_CROSS},
     NULL,
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},
    {{BAGL_ICON, 0x00, 117, 13, 8, 6, 0, 0, 0, 0xFFFFFF, 0x000000, 0,
      BAGL_GLYPH_ICON_CHECK},
     NULL,
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},

    //{{BAGL_ICON                           , 0x01,  21,   9,  14,  14, 0, 0, 0
    //, 0xFFFFFF, 0x000000, 0, BAGL_GLYPH_ICON_TRANSACTION_BADGE  }, NULL, 0, 0,
    //0, NULL, NULL, NULL },
    {{BAGL_LABELINE, 0x01, 0, 12, 128, 32, 0, 0, 0, 0xFFFFFF, 0x000000,
      BAGL_FONT_OPEN_SANS_EXTRABOLD_11px | BAGL_FONT_ALIGNMENT_CENTER, 0},
     "Confirm",
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},
    {{BAGL_LABELINE, 0x01, 0, 26, 128, 32, 0, 0, 0, 0xFFFFFF, 0x000000,
      BAGL_FONT_OPEN_SANS_EXTRABOLD_11px | BAGL_FONT_ALIGNMENT_CENTER, 0},
     (char *)confirmLabel,
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},

    {{BAGL_LABELINE, 0x02, 0, 12, 128, 32, 0, 0, 0, 0xFFFFFF, 0x000000,
      BAGL_FONT_OPEN_SANS_REGULAR_11px | BAGL_FONT_ALIGNMENT_CENTER, 0},
     "OP Type",
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},
    {{BAGL_LABELINE, 0x02, 23, 26, 82, 12, 0x80 | 10, 0, 0, 0xFFFFFF, 0x000000,
      BAGL_FONT_OPEN_SANS_EXTRABOLD_11px | BAGL_FONT_ALIGNMENT_CENTER, 26},
     (char *)txContent.opName,
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},

    {{BAGL_LABELINE, 0x03, 0, 12, 128, 32, 0, 0, 0, 0xFFFFFF, 0x000000,
      BAGL_FONT_OPEN_SANS_REGULAR_11px | BAGL_FONT_ALIGNMENT_CENTER, 0},
     (char *)txContent.arg.label,
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},
    {{BAGL_LABELINE, 0x03, 23, 26, 82, 12, 0x80 | 10, 0, 0, 0xFFFFFF, 0x000000,
      BAGL_FONT_OPEN_SANS_EXTRABOLD_11px | BAGL_FONT_ALIGNMENT_CENTER, 26},
     (char *)txContent.arg.data,
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},
};

unsigned int ui_single_action_tx_approval_prepro(const bagl_element_t *element)
{
    unsigned int display = 1;
    if (element->component.userid > 0)
    {
        if (ux_step > 2 && element->component.userid == 3) {
            display = 1;
        } else {
            display = (ux_step == element->component.userid - 1);
        }
        if (display)
        {
            switch (element->component.userid)
            {
            case 1:
            case 2:
                UX_CALLBACK_SET_INTERVAL(MAX(
                    3000, 1000 + bagl_label_roundtrip_duration_ms(element, 7)));
                break;

            case 3:
                UX_CALLBACK_SET_INTERVAL(MAX(
                    3000, 1000 + bagl_label_roundtrip_duration_ms(element, 7)));                
                printArgument(ux_step - 2, &txProcessingCtx);
                break;
            }
        }
    }
    return display;
}

const bagl_element_t ui_multiple_action_tx_approval_nanos[] = {
    // type                               userid    x    y   w    h  str rad
    // fill      fg        bg      fid iid  txt   touchparams...       ]
    {{BAGL_RECTANGLE, 0x00, 0, 0, 128, 32, 0, 0, BAGL_FILL, 0x000000, 0xFFFFFF,
      0, 0},
     NULL,
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},

    {{BAGL_ICON, 0x00, 3, 12, 7, 7, 0, 0, 0, 0xFFFFFF, 0x000000, 0,
      BAGL_GLYPH_ICON_CROSS},
     NULL,
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},
    {{BAGL_ICON, 0x00, 117, 13, 8, 6, 0, 0, 0, 0xFFFFFF, 0x000000, 0,
      BAGL_GLYPH_ICON_CHECK},
     NULL,
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},

    //{{BAGL_ICON                           , 0x01,  21,   9,  14,  14, 0, 0, 0
    //, 0xFFFFFF, 0x000000, 0, BAGL_GLYPH_ICON_TRANSACTION_BADGE  }, NULL, 0, 0,
    //0, NULL, NULL, NULL },
    {{BAGL_LABELINE, 0x01, 0, 12, 128, 32, 0, 0, 0, 0xFFFFFF, 0x000000,
      BAGL_FONT_OPEN_SANS_EXTRABOLD_11px | BAGL_FONT_ALIGNMENT_CENTER, 0},
     "Confirm",
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},
    {{BAGL_LABELINE, 0x01, 0, 26, 128, 32, 0, 0, 0, 0xFFFFFF, 0x000000,
      BAGL_FONT_OPEN_SANS_EXTRABOLD_11px | BAGL_FONT_ALIGNMENT_CENTER, 0},
     "Transaction",
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},

    {{BAGL_LABELINE, 0x02, 0, 12, 128, 32, 0, 0, 0, 0xFFFFFF, 0x000000,
      BAGL_FONT_OPEN_SANS_REGULAR_11px | BAGL_FONT_ALIGNMENT_CENTER, 0},
     "With",
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},
    {{BAGL_LABELINE, 0x02, 23, 26, 82, 12, 0x80 | 10, 0, 0, 0xFFFFFF, 0x000000,
      BAGL_FONT_OPEN_SANS_EXTRABOLD_11px | BAGL_FONT_ALIGNMENT_CENTER, 26},
     (char *)actionCounter,
     0,
     0,
     0,
     NULL,
     NULL,
     NULL},
};

unsigned int ui_multiple_action_tx_approval_prepro(const bagl_element_t *element)
{
    unsigned int display = 1;
    if (element->component.userid > 0)
    {
        display = (ux_step == element->component.userid - 1);
        if (display)
        {
            switch (element->component.userid)
            {
            case 1:
            case 2:
                UX_CALLBACK_SET_INTERVAL(2000);
                break;
            }
        }
    }
    return display;
}
#endif // #if defined(TARGET_NANOS)

#if defined(TARGET_NANOX)

void display_settings(void);
void switch_settings_contract_data(void);

UX_FLOW_DEF_NOCB(
    ux_idle_flow_1_step,
    bnn, //pnn,
    {
      "", //&C_icon_dashboard,
      "Application",
      "is ready",
    });
UX_FLOW_DEF_NOCB(
    ux_idle_flow_2_step,
    bn,
    {
      "Version",
      APPVERSION,
    });
UX_FLOW_DEF_VALID(
    ux_idle_flow_3_step,
    pb,
    display_settings(),
    {
      &C_icon_coggle,
      "Settings",
    });
UX_FLOW_DEF_VALID(
    ux_idle_flow_4_step,
    pb,
    os_sched_exit(-1),
    {
      &C_icon_dashboard_x,
      "Quit",
    });

UX_FLOW(
    ux_idle_flow,
    &ux_idle_flow_1_step,
    &ux_idle_flow_2_step,
    &ux_idle_flow_3_step,
    &ux_idle_flow_4_step
);

UX_FLOW_DEF_VALID(
    ux_settings_flow_1_step,
    bnnn,
    switch_settings_contract_data(),
    {
      "Contract data",
      "Allow contract data",
      "in transactions",
      confirmLabel,
    });

UX_FLOW_DEF_VALID(
    ux_settings_flow_2_step,
    pb,
    ui_idle(),
    {
      &C_icon_back_x,
      "Back",
    });

UX_FLOW(
    ux_settings_flow, 
    &ux_settings_flow_1_step,
    &ux_settings_flow_2_step
);

void display_settings() {
  strcpy(confirmLabel, (N_storage.dataAllowed ? "Allowed" : "NOT Allowed"));
  ux_flow_init(0, ux_settings_flow, NULL);
}

void switch_settings_contract_data() {
  uint8_t value = (N_storage.dataAllowed ? 0 : 1);
  nvm_write(&N_storage.dataAllowed, (void*)&value, sizeof(uint8_t));
  display_settings();
}

///////////////////////////////////////////////////////////////////////////////

UX_FLOW_DEF_NOCB(
    ux_display_public_flow_1_step,
    pnn,
    {
      &C_icon_eye,
      "Verify",
      "Public Key",
    });
UX_FLOW_DEF_NOCB(
    ux_display_public_flow_2_step,
    bnnn_paging,
    {
      .title = "Public Key",
      .text = tmpCtx.publicKeyContext.address,
    });
UX_FLOW_DEF_VALID(
    ux_display_public_flow_3_step,
    pb,
    io_seproxyhal_touch_address_ok(NULL),
    {
      &C_icon_validate_14,
      "Approve",
    });
UX_FLOW_DEF_VALID(
    ux_display_public_flow_4_step,
    pb,
    io_seproxyhal_touch_address_cancel(NULL),
    {
      &C_icon_crossmark,
      "Reject",
    });

UX_FLOW(
    ux_display_public_flow,
    &ux_display_public_flow_1_step,
    &ux_display_public_flow_2_step,
    &ux_display_public_flow_3_step,
    &ux_display_public_flow_4_step
);

///////////////////////////////////////////////////////////////////////////////

#define STATE_LEFT_BORDER 0
#define STATE_VARIABLE 1
#define STATE_RIGHT_BORDER 2

volatile char confirm_text1[16];
volatile char confirm_text2[16];

void display_next_state(uint8_t state);
void ux_single_action_sign_flow_ok_pressed();

UX_FLOW_DEF_NOCB(
    ux_single_action_sign_flow_1_step,
    pnn,
    {
      &C_icon_certificate,
      "Review",
      confirmLabel,
    });
UX_FLOW_DEF_NOCB(
    ux_single_action_sign_flow_2_step,
    bn,
    {
      "OP Type",
      txContent.opName,
    });
UX_STEP_INIT(
    ux_init_left_border,
    NULL,
    NULL,
    {
        display_next_state(STATE_LEFT_BORDER);
    });

UX_STEP_NOCB_INIT(
    ux_single_action_sign_flow_variable_step,
    bnnn_paging,
    {
        display_next_state(STATE_VARIABLE);
    },
    {
      .title = txContent.arg.label,
      .text = txContent.arg.data,
    });

UX_STEP_INIT(
    ux_init_right_border,
    NULL,
    NULL,
    {
        display_next_state(STATE_RIGHT_BORDER);
    });

UX_FLOW_DEF_VALID(
    ux_single_action_sign_flow_6_step,
    pbb,
    ux_single_action_sign_flow_ok_pressed(),
    {
      &C_icon_validate_14,
      confirm_text1,
      confirm_text2,
    });
UX_FLOW_DEF_VALID(
    ux_single_action_sign_flow_7_step,
    pbb,
    io_seproxyhal_touch_tx_cancel(NULL),
    {
      &C_icon_crossmark,
      "Cancel",
      "signature",
    });

UX_FLOW(
    ux_single_action_sign_flow, 
    &ux_single_action_sign_flow_1_step,
    &ux_single_action_sign_flow_2_step,
    &ux_init_left_border,
    &ux_single_action_sign_flow_variable_step,
    &ux_init_right_border,
    &ux_single_action_sign_flow_6_step,
    &ux_single_action_sign_flow_7_step
);

void display_next_state(uint8_t state) 
{    
    if (state == STATE_LEFT_BORDER)
    {
        if (ux_step == 0)
        {
            ux_step = 1;
            ux_flow_next();
        }
        else if (ux_step == 1) 
        {
            --ux_step;
            ux_flow_prev();
        }
        else if (ux_step > 1)
        {
            --ux_step;
            ux_flow_next();
        }
    }
    else if (state == STATE_VARIABLE)
    {
        printArgument(ux_step-1, &txProcessingCtx);
    }
    else if (state == STATE_RIGHT_BORDER)
    {
        if (ux_step < ux_step_count)
        {
            ++ux_step;
            ux_flow_prev();
        }
        else if (ux_step == ux_step_count)
        {
            ++ux_step;
            ux_flow_next();
        }
        else if (ux_step > ux_step_count)
        {
            ux_step = ux_step_count;
            ux_flow_prev();
        }
    }
}

void ux_single_action_sign_flow_ok_pressed() 
{
    parserStatus_e txResult = parseTx(&txProcessingCtx, NULL, 0);
    switch (txResult) {
    case STREAM_ACTION_READY:
        ux_step = 0;
        ux_step_count = 1 + txContent.argumentCount;
        if (txProcessingCtx.numOperations > 1) {
            snprintf((char *)confirmLabel, sizeof(confirmLabel), "OP #%d", txProcessingCtx.currentOpIndex);
        }
        strcpy((char *)confirm_text1, txProcessingCtx.currentOpIndex == txProcessingCtx.numOperations ? "Sign" : "Accept");
        strcpy((char *)confirm_text2, txProcessingCtx.currentOpIndex == txProcessingCtx.numOperations ? "transaction" : "and review next");

        ux_flow_init(0, ux_single_action_sign_flow, NULL);
        break;
    case STREAM_PROCESSING:
        io_exchange_with_code(0x9000, 0);
        // Display back the original UX
        ui_idle();
        break;
    case STREAM_FINISHED:
        if(++ux_step < ux_step_count) {
            printArgument(ux_step, &txProcessingCtx);
            UX_REDISPLAY();
            return 0;
        }

        io_seproxyhal_touch_tx_ok(NULL);
        break;
    default:
        io_exchange_with_code(0x6A80, 0);
        // Display back the original UX
        ui_idle();
        break;
    }
}


///////////////////////////////////////////////////////////////////////////////

void ux_multiple_action_sign_flow_ok_pressed();

UX_FLOW_DEF_NOCB(
    ux_multiple_action_sign_flow_1_step,
    pnn,
    {
      &C_icon_certificate,
      "Review",
      "Transaction",
    });
UX_FLOW_DEF_NOCB(
    ux_multiple_action_sign_flow_2_step,
    bn, //pnn,
    {
      "With",
      actionCounter,
    });
UX_FLOW_DEF_VALID(
    ux_multiple_action_sign_flow_3_step,
    pbb,
    ux_multiple_action_sign_flow_ok_pressed(),
    {
      &C_icon_validate_14,
      "Continue",
      "review"
    });
UX_FLOW_DEF_VALID(
    ux_multiple_action_sign_flow_4_step,
    pbb,
    io_seproxyhal_touch_tx_cancel(NULL),
    {
      &C_icon_crossmark,
      "Cancel",
      "review",
    });

UX_FLOW(
    ux_multiple_action_sign_flow, 
    &ux_multiple_action_sign_flow_1_step,
    &ux_multiple_action_sign_flow_2_step,
    &ux_multiple_action_sign_flow_3_step,
    &ux_multiple_action_sign_flow_4_step
);

void ux_multiple_action_sign_flow_ok_pressed()
{
    parserStatus_e txResult = parseTx(&txProcessingCtx, NULL, 0);
    switch (txResult) {
    case STREAM_ACTION_READY:
        ux_step = 0;
        ux_step_count = txContent.argumentCount;
        // TODO: proper redisplya
        // UX_REDISPLAY();
        break;
    case STREAM_PROCESSING:
        io_exchange_with_code(0x9000, 0);
        // Display back the original UX
        ui_idle();
        break;
    case STREAM_FINISHED:
        io_seproxyhal_touch_tx_ok(NULL);
        break;
    default:
        io_exchange_with_code(0x6A80, 0);
        // Display back the original UX
        ui_idle();
        break;
    }
}

#endif // #if defined(TARGET_NANOX)

void ui_idle(void)
{
#if defined(TARGET_NANOS)
    UX_MENU_DISPLAY(0, menu_main, NULL);
#elif defined(TARGET_NANOX)
    // reserve a display stack slot if none yet
    if(G_ux.stack_count == 0) {
        ux_stack_push();
    }
    ux_flow_init(0, ux_idle_flow, NULL);
#endif
}

unsigned int io_seproxyhal_touch_exit(const bagl_element_t *e)
{
    // Go back to the dashboard
    os_sched_exit(0);
    return 0; // do not redraw the widget
}

unsigned int io_seproxyhal_touch_address_ok(const bagl_element_t *e)
{
    uint32_t tx = get_public_key_and_set_result();
    io_exchange_with_code(0x9000, tx);
    // Display back the original UX
    ui_idle();
    return 0; // do not redraw the widget
}

unsigned int io_seproxyhal_touch_address_cancel(const bagl_element_t *e)
{
    io_exchange_with_code(0x6985, 0);
    // Display back the original UX
    ui_idle();
    return 0; // do not redraw the widget
}

#if defined(TARGET_NANOS)
unsigned int ui_address_nanos_button(unsigned int button_mask,
                                     unsigned int button_mask_counter)
{
    switch (button_mask)
    {
    case BUTTON_EVT_RELEASED | BUTTON_LEFT: // CANCEL
        io_seproxyhal_touch_address_cancel(NULL);
        break;

    case BUTTON_EVT_RELEASED | BUTTON_RIGHT:
    { // OK
        io_seproxyhal_touch_address_ok(NULL);
        break;
    }
    }
    return 0;
}
#endif // ui_address_nanos_button

unsigned int io_seproxyhal_touch_tx_ok(const bagl_element_t *e)
{
    uint32_t tx = sign_hash_and_set_result();
    io_exchange_with_code(0x9000, tx);
    // Display back the original UX
    ui_idle();

    return 0; // do not redraw the widge
}

unsigned int io_seproxyhal_touch_tx_cancel(const bagl_element_t *e)
{
    io_exchange_with_code(0x6985, 0);
    // Display back the original UX
    ui_idle();
    return 0; // do not redraw the widget
}

#if defined(TARGET_NANOS)

unsigned int ui_single_action_tx_approval_nanos_button(unsigned int button_mask,
                                      unsigned int button_mask_counter)
{
    switch (button_mask)
    {
    case BUTTON_EVT_RELEASED | BUTTON_LEFT:
        io_seproxyhal_touch_tx_cancel(NULL);
        break;

    case BUTTON_EVT_RELEASED | BUTTON_RIGHT:
        {
            // Proceed to next ux_step if not at end
            if(++ux_step < ux_step_count) {
                printArgument(ux_step, &txProcessingCtx);
                UX_REDISPLAY();
                return 0;
            }

            parserStatus_e txResult = parseTx(&txProcessingCtx, NULL, 0);
            switch (txResult) {
            case STREAM_ACTION_READY:
                ux_step = 0;
                ux_step_count = 2 + txContent.argumentCount;
                if (txProcessingCtx.numOperations > 1) {
                    snprintf((char *)confirmLabel, sizeof(confirmLabel), "OP #%d", txProcessingCtx.currentOpIndex);
                }
                UX_REDISPLAY();
                break;
            case STREAM_PROCESSING:
                io_exchange_with_code(0x9000, 0);
                // Display back the original UX
                ui_idle();
                break;
            case STREAM_FINISHED:
                io_seproxyhal_touch_tx_ok(NULL);
                break;
            default:
                io_exchange_with_code(0x6A80, 0);
                // Display back the original UX
                ui_idle();
                break;
            }
        }
        break;

    }
    return 0;
}

unsigned int ui_multiple_action_tx_approval_nanos_button(unsigned int button_mask,
                                      unsigned int button_mask_counter) 
{
    switch (button_mask) 
    {
    case BUTTON_EVT_RELEASED | BUTTON_LEFT:
        io_seproxyhal_touch_tx_cancel(NULL);
        break;

    case BUTTON_EVT_RELEASED | BUTTON_RIGHT:
        {
            parserStatus_e txResult = parseTx(&txProcessingCtx, NULL, 0);
            switch (txResult) {
            case STREAM_ACTION_READY:
                ux_step = 0;
                ux_step_count = txContent.argumentCount;
                UX_REDISPLAY();
                break;
            case STREAM_PROCESSING:
                io_exchange_with_code(0x9000, 0);
                // Display back the original UX
                ui_idle();
                break;
            case STREAM_FINISHED:
                io_seproxyhal_touch_tx_ok(NULL);
                break;
            default:
                io_exchange_with_code(0x6A80, 0);
                // Display back the original UX
                ui_idle();
                break;
            }
        }
    }

    return 0;
}

#endif // defined(TARGET_NANOS)

void io_exchange_with_code(uint16_t code, uint32_t tx) {
	G_io_apdu_buffer[tx++] = code >> 8;
	G_io_apdu_buffer[tx++] = code & 0xFF;
    // Send back the response, do not restart the event loop
	io_exchange(CHANNEL_APDU | IO_RETURN_AFTER_TX, tx);
}

unsigned short io_exchange_al(unsigned char channel, unsigned short tx_len)
{
    switch (channel & ~(IO_FLAGS))
    {
    case CHANNEL_KEYBOARD:
        break;

    // multiplexed io exchange over a SPI channel and TLV encapsulated protocol
    case CHANNEL_SPI:
        if (tx_len)
        {
            io_seproxyhal_spi_send(G_io_apdu_buffer, tx_len);

            if (channel & IO_RESET_AFTER_REPLIED)
            {
                reset();
            }
            return 0; // nothing received from the master so far (it's a tx
                      // transaction)
        }
        else
        {
            return io_seproxyhal_spi_recv(G_io_apdu_buffer,
                                          sizeof(G_io_apdu_buffer), 0);
        }

    default:
        THROW(INVALID_PARAMETER);
    }
    return 0;
}

uint32_t get_public_key_and_set_result()
{
    uint32_t tx = 0;
    G_io_apdu_buffer[tx++] = 65;
    os_memmove(G_io_apdu_buffer + tx, tmpCtx.publicKeyContext.publicKey.W, 65);
    tx += 65;

    uint32_t addressLength = strlen(tmpCtx.publicKeyContext.address);

    G_io_apdu_buffer[tx++] = addressLength;
    os_memmove(G_io_apdu_buffer + tx, tmpCtx.publicKeyContext.address, addressLength);
    tx += addressLength;
    if (tmpCtx.publicKeyContext.getChaincode)
    {
        os_memmove(G_io_apdu_buffer + tx, tmpCtx.publicKeyContext.chainCode, 32);
        tx += 32;
    }
    return tx;
}

void handleGetPublicKey(uint8_t p1, uint8_t p2, uint8_t *dataBuffer,
                        uint16_t dataLength, volatile unsigned int *flags,
                        volatile unsigned int *tx)
{
    UNUSED(dataLength);
    uint8_t privateKeyData[32];
    uint32_t bip32Path[MAX_BIP32_PATH];
    uint32_t i;
    uint8_t bip32PathLength = *(dataBuffer++);
    cx_ecfp_private_key_t privateKey;

    if ((bip32PathLength < 0x01) || (bip32PathLength > MAX_BIP32_PATH))
    {
        PRINTF("Invalid path\n");
        THROW(0x6a80);
    }
    if ((p1 != P1_CONFIRM) && (p1 != P1_NON_CONFIRM))
    {
        THROW(0x6B00);
    }
    if ((p2 != P2_CHAINCODE) && (p2 != P2_NO_CHAINCODE))
    {
        THROW(0x6B00);
    }
    for (i = 0; i < bip32PathLength; i++)
    {
        bip32Path[i] = (dataBuffer[0] << 24) | (dataBuffer[1] << 16) |
                       (dataBuffer[2] << 8) | (dataBuffer[3]);
        dataBuffer += 4;
    }
    tmpCtx.publicKeyContext.getChaincode = (p2 == P2_CHAINCODE);
    os_perso_derive_node_bip32(CX_CURVE_256K1, bip32Path, bip32PathLength,
                               privateKeyData,
                               (tmpCtx.publicKeyContext.getChaincode
                                    ? tmpCtx.publicKeyContext.chainCode
                                    : NULL));
    cx_ecfp_init_private_key(CX_CURVE_256K1, privateKeyData, 32, &privateKey);
    cx_ecfp_generate_pair(CX_CURVE_256K1, &tmpCtx.publicKeyContext.publicKey,
                          &privateKey, 1);
    os_memset(&privateKey, 0, sizeof(privateKey));
    os_memset(privateKeyData, 0, sizeof(privateKeyData));
    public_key_to_wif(tmpCtx.publicKeyContext.publicKey.W, sizeof(tmpCtx.publicKeyContext.publicKey.W),
                      tmpCtx.publicKeyContext.address, sizeof(tmpCtx.publicKeyContext.address));
    if (p1 == P1_NON_CONFIRM)
    {
        *tx = get_public_key_and_set_result();
        THROW(0x9000);
    }
    else
    {
#if defined(TARGET_NANOS)
        // prepare for a UI based reply
        ux_step = 0;
        ux_step_count = 2;
        UX_DISPLAY(ui_address_nanos, ui_address_prepro);
#elif defined(TARGET_NANOX)
        ux_flow_init(0, ux_display_public_flow, NULL);
#endif

        *flags |= IO_ASYNCH_REPLY;
    }
}

void handleGetAppConfiguration(uint8_t p1, uint8_t p2, uint8_t *workBuffer,
                               uint16_t dataLength,
                               volatile unsigned int *flags,
                               volatile unsigned int *tx)
{
    UNUSED(p1);
    UNUSED(p2);
    UNUSED(workBuffer);
    UNUSED(dataLength);
    UNUSED(flags);
    G_io_apdu_buffer[0] = (N_storage.dataAllowed ? 0x01 : 0x00);
    G_io_apdu_buffer[1] = LEDGER_MAJOR_VERSION;
    G_io_apdu_buffer[2] = LEDGER_MINOR_VERSION;
    G_io_apdu_buffer[3] = LEDGER_PATCH_VERSION;
    *tx = 4;
    THROW(0x9000);
}

uint32_t sign_hash_and_set_result(void) 
{
    // store hash
    cx_hash(&sha256.header, CX_LAST, tmpCtx.transactionContext.hash, 0, 
        tmpCtx.transactionContext.hash, sizeof(tmpCtx.transactionContext.hash));

    uint8_t privateKeyData[64];
    cx_ecfp_private_key_t privateKey;
    uint32_t tx = 0;
    uint8_t V[33];
    uint8_t K[32];
    int tries = 0;

    os_perso_derive_node_bip32(
        CX_CURVE_256K1, tmpCtx.transactionContext.bip32Path,
        tmpCtx.transactionContext.pathLength, privateKeyData, NULL);
    cx_ecfp_init_private_key(CX_CURVE_256K1, privateKeyData, 32, &privateKey);
    os_memset(privateKeyData, 0, sizeof(privateKeyData));

    // Loop until a candidate matching the canonical signature is found

    for (;;)
    {
        if (tries == 0)
        {
            rng_rfc6979(G_io_apdu_buffer + 100, tmpCtx.transactionContext.hash, privateKey.d, privateKey.d_len, SECP256K1_N, 32, V, K);
        }
        else
        {
            rng_rfc6979(G_io_apdu_buffer + 100, tmpCtx.transactionContext.hash, NULL, 0, SECP256K1_N, 32, V, K);
        }
        uint32_t infos;
        tx = cx_ecdsa_sign(&privateKey, CX_NO_CANONICAL | CX_RND_PROVIDED | CX_LAST, CX_SHA256,
                           tmpCtx.transactionContext.hash, 32, 
                           G_io_apdu_buffer + 100, 100,
                           &infos);
        if ((infos & CX_ECCINFO_PARITY_ODD) != 0)
        {
            G_io_apdu_buffer[100] |= 0x01;
        }
        G_io_apdu_buffer[0] = 27 + 4 + (G_io_apdu_buffer[100] & 0x01);
        ecdsa_der_to_sig(G_io_apdu_buffer + 100, G_io_apdu_buffer + 1);
        if (check_canonical(G_io_apdu_buffer + 1))
        {
            tx = 1 + 64;
            break;
        }
        else
        {
            tries++;
        }
    }

    os_memset(&privateKey, 0, sizeof(privateKey));

    return tx;
}

void handleSign(uint8_t p1, uint8_t p2, uint8_t *workBuffer,
                uint16_t dataLength, volatile unsigned int *flags,
                volatile unsigned int *tx)
{
    uint32_t i;
    parserStatus_e txResult;
    if (p1 == P1_FIRST)
    {
        tmpCtx.transactionContext.pathLength = workBuffer[0];
        if ((tmpCtx.transactionContext.pathLength < 0x01) ||
            (tmpCtx.transactionContext.pathLength > MAX_BIP32_PATH))
        {
            PRINTF("Invalid path\n");
            THROW(0x6a80);
        }
        workBuffer++;
        dataLength--;
        for (i = 0; i < tmpCtx.transactionContext.pathLength; i++)
        {
            tmpCtx.transactionContext.bip32Path[i] =
                (workBuffer[0] << 24) | (workBuffer[1] << 16) |
                (workBuffer[2] << 8) | (workBuffer[3]);
            workBuffer += 4;
            dataLength -= 4;
        }
        initTxContext(&txProcessingCtx, &sha256, &dataSha256, &txContent, N_storage.dataAllowed);
    }
    else if (p1 != P1_MORE)
    {
        THROW(0x6B00);
    }
    if (p2 != 0)
    {
        THROW(0x6B00);
    }
    if (txProcessingCtx.state == TLV_NONE)
    {
        PRINTF("Parser not initialized\n");
        THROW(0x6985);
    }

    txResult = parseTx(&txProcessingCtx, workBuffer, dataLength);
    switch (txResult)
    {
    case STREAM_CONFIRM_PROCESSING:
        snprintf((char *)actionCounter, sizeof(actionCounter), "%d operations", txProcessingCtx.numOperations);
#if defined(TARGET_NANOS)
        ux_step = 0;
        ux_step_count = 2;
        UX_DISPLAY(ui_multiple_action_tx_approval_nanos, ui_multiple_action_tx_approval_prepro);
#elif defined(TARGET_NANOX)
        ux_flow_init(0, ux_multiple_action_sign_flow, NULL);
#endif

        *flags |= IO_ASYNCH_REPLY;

        break;
    case STREAM_ACTION_READY:
        ux_step = 0;
        ux_step_count = txContent.argumentCount;

        if (txProcessingCtx.numOperations > 1) {
            snprintf((char *)confirmLabel, sizeof(confirmLabel), "Action #%d", txProcessingCtx.currentOpIndex);
        } else {
            strcpy((char *)confirmLabel, "Transaction");         
        }

#if defined(TARGET_NANOS)
        ux_step_count += 2;
        UX_DISPLAY(ui_single_action_tx_approval_nanos, ui_single_action_tx_approval_prepro);
#elif defined(TARGET_NANOX)
        strcpy((char *)confirm_text1, txProcessingCtx.currentOpIndex == txProcessingCtx.numOperations ? "Sign" : "Accept");
        strcpy((char *)confirm_text2, txProcessingCtx.currentOpIndex == txProcessingCtx.numOperations ? "transaction" : "and review next");
        
        ux_flow_init(0, ux_single_action_sign_flow, NULL);
#endif

        *flags |= IO_ASYNCH_REPLY;

        break;
    case STREAM_FINISHED:
        *tx = sign_hash_and_set_result();
        THROW(0x9000);
    case STREAM_PROCESSING:
        THROW(0x9000);
    case STREAM_FAULT:
        THROW(0x6A80);
    default:
        PRINTF("Unexpected parser status\n");
        THROW(0x6A80);
    }
}

void handleApdu(volatile unsigned int *flags, volatile unsigned int *tx)
{
    unsigned short sw = 0;

    BEGIN_TRY
    {
        TRY
        {
            if (G_io_apdu_buffer[OFFSET_CLA] != CLA)
            {
                THROW(0x6E00);
            }

            switch (G_io_apdu_buffer[OFFSET_INS])
            {
            case INS_GET_PUBLIC_KEY:
                handleGetPublicKey(G_io_apdu_buffer[OFFSET_P1],
                                   G_io_apdu_buffer[OFFSET_P2],
                                   G_io_apdu_buffer + OFFSET_CDATA,
                                   G_io_apdu_buffer[OFFSET_LC], flags, tx);
                break;

            case INS_SIGN:
                handleSign(G_io_apdu_buffer[OFFSET_P1],
                           G_io_apdu_buffer[OFFSET_P2],
                           G_io_apdu_buffer + OFFSET_CDATA,
                           G_io_apdu_buffer[OFFSET_LC], flags, tx);
                break;

            case INS_GET_APP_CONFIGURATION:
                handleGetAppConfiguration(
                    G_io_apdu_buffer[OFFSET_P1], 
                    G_io_apdu_buffer[OFFSET_P2],
                    G_io_apdu_buffer + OFFSET_CDATA,
                    G_io_apdu_buffer[OFFSET_LC], flags, tx);
                break;

            default:
                THROW(0x6D00);
                break;
            }
        }
        CATCH(EXCEPTION_IO_RESET)
        {
            THROW(EXCEPTION_IO_RESET);
        }
        CATCH_OTHER(e)
        {
            switch (e & 0xF000)
            {
            case 0x6000:
                // Wipe the transaction context and report the exception
                sw = e;
                break;
            case 0x9000:
                // All is well
                sw = e;
                break;
            default:
                // Internal error
                sw = 0x6800 | (e & 0x7FF);
                break;
            }
            // Unexpected exception => report
            G_io_apdu_buffer[*tx] = sw >> 8;
            G_io_apdu_buffer[*tx + 1] = sw;
            *tx += 2;
        }
        FINALLY
        {
        }
    }
    END_TRY;
}

void sample_main(void)
{
    volatile unsigned int rx = 0;
    volatile unsigned int tx = 0;
    volatile unsigned int flags = 0;

    // DESIGN NOTE: the bootloader ignores the way APDU are fetched. The only
    // goal is to retrieve APDU.
    // When APDU are to be fetched from multiple IOs, like NFC+USB+BLE, make
    // sure the io_event is called with a
    // switch event, before the apdu is replied to the bootloader. This avoid
    // APDU injection faults.
    for (;;)
    {
        volatile unsigned short sw = 0;

        BEGIN_TRY
        {
            TRY
            {
                rx = tx;
                tx = 0; // ensure no race in catch_other if io_exchange throws
                        // an error
                rx = io_exchange(CHANNEL_APDU | flags, rx);
                flags = 0;

                // no apdu received, well, reset the session, and reset the
                // bootloader configuration
                if (rx == 0)
                {
                    THROW(0x6982);
                }

                handleApdu(&flags, &tx);
            }
            CATCH(EXCEPTION_IO_RESET)
            {
                THROW(EXCEPTION_IO_RESET);
            }
            CATCH_OTHER(e)
            {
                switch (e & 0xF000)
                {
                case 0x6000:
                    // Wipe the transaction context and report the exception
                    sw = e;
                    break;
                case 0x9000:
                    // All is well
                    sw = e;
                    break;
                default:
                    // Internal error
                    sw = 0x6800 | (e & 0x7FF);
                    break;
                }
                // Unexpected exception => report
                G_io_apdu_buffer[tx] = sw >> 8;
                G_io_apdu_buffer[tx + 1] = sw;
                tx += 2;
            }
            FINALLY
            {
            }
        }
        END_TRY;
    }

    // return_to_dashboard:
    return;
}

// override point, but nothing more to do
void io_seproxyhal_display(const bagl_element_t *element)
{
    io_seproxyhal_display_default((bagl_element_t *)element);
}

unsigned char io_event(unsigned char channel)
{
    // nothing done with the event, throw an error on the transport layer if
    // needed

    // can't have more than one tag in the reply, not supported yet.
    switch (G_io_seproxyhal_spi_buffer[0])
    {
    case SEPROXYHAL_TAG_FINGER_EVENT:
        UX_FINGER_EVENT(G_io_seproxyhal_spi_buffer);
        break;

    case SEPROXYHAL_TAG_BUTTON_PUSH_EVENT:
        UX_BUTTON_PUSH_EVENT(G_io_seproxyhal_spi_buffer);
        break;

    case SEPROXYHAL_TAG_STATUS_EVENT:
        if (G_io_apdu_media == IO_APDU_MEDIA_USB_HID &&
            !(U4BE(G_io_seproxyhal_spi_buffer, 3) &
              SEPROXYHAL_TAG_STATUS_EVENT_FLAG_USB_POWERED))
        {
            THROW(EXCEPTION_IO_RESET);
        }
    // no break is intentional
    default:
        UX_DEFAULT_EVENT();
        break;

    case SEPROXYHAL_TAG_DISPLAY_PROCESSED_EVENT:
        UX_DISPLAYED_EVENT({});
        break;

    case SEPROXYHAL_TAG_TICKER_EVENT:
        UX_TICKER_EVENT(G_io_seproxyhal_spi_buffer, {
#if defined(TARGET_NANOS)
            if (UX_ALLOWED)
            {
                if (ux_step_count)
                {
                    // prepare next screen
                    ux_step = (ux_step + 1) % ux_step_count;
                    // redisplay screen
                    UX_REDISPLAY();
                }
            }
#endif // TARGET_NANOS
        });
        break;
    }

    // close the event if not done previously (by a display or whatever)
    if (!io_seproxyhal_spi_is_status_sent())
    {
        io_seproxyhal_general_status();
    }

    // command has been processed, DO NOT reset the current APDU transport
    return 1;
}

void app_exit(void)
{
    BEGIN_TRY_L(exit)
    {
        TRY_L(exit)
        {
            os_sched_exit(-1);
        }
        FINALLY_L(exit)
        {
        }
    }
    END_TRY_L(exit);
}

__attribute__((section(".boot"))) int main(void)
{
    // exit critical section
    __asm volatile("cpsie i");

    for (;;)
    {
        UX_INIT();

        // ensure exception will work as planned
        os_boot();

        BEGIN_TRY
        {
            TRY
            {
                io_seproxyhal_init();
#ifdef TARGET_NANOX
                // grab the current plane mode setting
                G_io_app.plane_mode = os_setting_get(OS_SETTING_PLANEMODE, NULL, 0);
#endif // TARGET_NANOX

                if (N_storage.initialized != 0x01)
                {
                    internalStorage_t storage;
                    storage.dataAllowed = 0x00;
                    storage.initialized = 0x01;
                    nvm_write(&N_storage, (void *)&storage,
                              sizeof(internalStorage_t));
                }

                USB_power(0);
                USB_power(1);

                ui_idle();

#ifdef HAVE_BLE
                BLE_power(0, NULL);
                BLE_power(1, "Nano X");
#endif // HAVE_BLE

                sample_main();
            }
            CATCH(EXCEPTION_IO_RESET)
            {
                // reset IO and UX before continuing
                continue;
            }
            CATCH_ALL
            {
                break;
            }
            FINALLY
            {
            }
        }
        END_TRY;
    }
    app_exit();

    return 0;
}
