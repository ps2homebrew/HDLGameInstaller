enum SYS_UI_MESSAGES {
    SYS_UI_MSG_INIT_ERROR = 0,
    SYS_UI_MSG_PROCEED,
    SYS_UI_MSG_CANCEL_INPUT,
    SYS_UI_MSG_FULL_GAME_TITLE,
    SYS_UI_MSG_OSD_TITLE_1,
    SYS_UI_MSG_CHANGE_ICON,
    SYS_UI_MSG_ICON_LOAD_FAIL,
    SYS_UI_MSG_OSD_INST_FAILED,
    SYS_UI_MSG_NO_HDD,
    SYS_UI_MSG_HDD_FORMAT,
    SYS_UI_MSG_QUIT,
    SYS_UI_MSG_GAME_UPDATE_COMPLETE,
    SYS_UI_MSG_GAME_UPDATE_FAIL,
    SYS_UI_MSG_PROMPT_DELETE_GAME,
    SYS_UI_MSG_PLEASE_WAIT,
    SYS_UI_MSG_INST_OVERWRITE,
    SYS_UI_MSG_INST_COMPLETE,
    SYS_UI_MSG_UNSUP_DISC,
    SYS_UI_MSG_NO_DISC,
    SYS_UI_MSG_READING_DISC,
    SYS_UI_MSG_DISC_READ_ERR,
    SYS_UI_MSG_SYS_CNF_PARSE_FAIL,
    SYS_UI_MSG_CANCEL_INST,
    SYS_UI_MSG_DISC_READ_FAULT,
    SYS_UI_MSG_HDD_WRITE_FAULT,
    SYS_UI_MSG_LACK_OF_MEM,
    SYS_UI_MSG_INST_CANCELLED,
    SYS_UI_MSG_PART_ACC_ERR,
    SYS_UI_MSG_REMOTE_CONN,
    SYS_UI_MSG_NO_ICONS,
    SYS_UI_MSG_NO_NETWORK_CONNECTION,
    SYS_UI_MSG_CONNECTING,
    SYS_UI_MSG_DHCP_ERROR,
    SYS_UI_MSG_INSUFFICIENT_HDD_SPACE,
    SYS_UI_MSG_SAVING_HDD,
    SYS_UI_MSG_ERROR_SAVING_SETTINGS,

    SYS_UI_MSG_COUNT
};

enum SYS_UI_LABEL_TEXT {
    SYS_UI_LBL_OK = 0,
    SYS_UI_LBL_CANCEL,
    SYS_UI_LBL_YES,
    SYS_UI_LBL_NO,
    SYS_UI_LBL_NEXT,
    SYS_UI_LBL_BACK,
    SYS_UI_LBL_ENABLED,
    SYS_UI_LBL_DISABLED,
    SYS_UI_LBL_TOGGLE_OPTION,
    SYS_UI_LBL_SELECT_FIELD,
    SYS_UI_LBL_SELECT_GAME,
    SYS_UI_LBL_GAME_OPTIONS,
    SYS_UI_LBL_DELETE_GAME,
    SYS_UI_LBL_INSTALL_GAME,
    SYS_UI_LBL_SELECT_KEY,
    SYS_UI_LBL_MOVE_CURSOR,
    SYS_UI_LBL_TOGGLE_CHAR_SET,
    SYS_UI_LBL_ENTER,
    SYS_UI_LBL_DELETE_CHAR,
    SYS_UI_LBL_INSERT_SPACE,
    SYS_UI_LBL_QUIT,
    SYS_UI_LBL_SELECT_ICON,
    SYS_UI_LBL_SELECT_DEVICE,
    SYS_UI_LBL_WARNING,
    SYS_UI_LBL_ERROR,
    SYS_UI_LBL_INFO,
    SYS_UI_LBL_CONFIRM,
    SYS_UI_LBL_WAIT,
    SYS_UI_LBL_REMOTE_CONN,
    SYS_UI_LBL_ICON_SOURCE,
    SYS_UI_LBL_NOW_INSTALLING,
    SYS_UI_LBL_TITLE,
    SYS_UI_LBL_DISC_ID,
    SYS_UI_LBL_DISC_TYPE,
    SYS_UI_LBL_RATE,
    SYS_UI_LBL_TIME_REMAINING,
    SYS_UI_LBL_INST_FULL_TITLE,
    SYS_UI_LBL_INST_OSD_TITLE_1,
    SYS_UI_LBL_INST_OSD_TITLE_2,
    SYS_UI_LBL_INST_OPTION_1,
    SYS_UI_LBL_INST_OPTION_2,
    SYS_UI_LBL_INST_OPTION_3,
    SYS_UI_LBL_INST_OPTION_4,
    SYS_UI_LBL_INST_OPTION_5,
    SYS_UI_LBL_INST_OPTION_6,
    SYS_UI_LBL_INST_OPTION_7,
    SYS_UI_LBL_INST_OPTION_8,
    SYS_UI_LBL_INST_TRANSFER_OPTION,
    SYS_UI_LBL_DEV_MC,
    SYS_UI_LBL_MC_SLOT_1,
    SYS_UI_LBL_MC_SLOT_2,
    SYS_UI_LBL_DEV_MASS,
    SYS_UI_LBL_ICON_SEL_DEFAULT,
    SYS_UI_LBL_ICON_SEL_SAVE_DATA,
    SYS_UI_LBL_ICON_SEL_EXTERNAL,
    SYS_UI_LBL_NETWORK_STATUS,
    SYS_UI_LBL_MAC_ADDRESS,
    SYS_UI_LBL_IP_ADDRESS,
    SYS_UI_LBL_NM_ADDRESS,
    SYS_UI_LBL_GW_ADDRESS,
    SYS_UI_LBL_LINK_STATE,
    SYS_UI_LBL_LINK_MODE,
    SYS_UI_LBL_UP,
    SYS_UI_LBL_DOWN,
    SYS_UI_LBL_UNKNOWN,
    SYS_UI_LBL_MODE_10MBIT_HDX,
    SYS_UI_LBL_MODE_10MBIT_FDX,
    SYS_UI_LBL_MODE_100MBIT_HDX,
    SYS_UI_LBL_MODE_100MBIT_FDX,
    SYS_UI_LBL_FLOW_CONTROL,
    SYS_UI_LBL_DROPPED_TX_FRAMES,
    SYS_UI_LBL_DROPPED_RX_FRAMES,
    SYS_UI_LBL_TX_LOSSCR,
    SYS_UI_LBL_RX_OVERRUN,
    SYS_UI_LBL_TX_EDEFER,
    SYS_UI_LBL_RX_BADLEN,
    SYS_UI_LBL_TX_COLLISON,
    SYS_UI_LBL_RX_BADFCS,
    SYS_UI_LBL_TX_UNDERRUN,
    SYS_UI_LBL_RX_BADALIGN,
    SYS_UI_LBL_MODE_AUTO,
    SYS_UI_LBL_OPTIONS,
    SYS_UI_LBL_NETWORK_OPTIONS,
    SYS_UI_LBL_ADVANCED_SETTINGS,
    SYS_UI_LBL_USE_DHCP,
    SYS_UI_LBL_ADDRESS_TYPE,
    SYS_UI_LBL_SORT_TITLES,
    SYS_UI_LBL_IP_STATIC,
    SYS_UI_LBL_IP_DHCP,

    SYS_UI_LBL_COUNT
};
