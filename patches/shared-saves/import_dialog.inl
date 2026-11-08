bool ImportAdventureWithDialog() {
    if(SharedSaveBusy())return false;
    if(NFD_Init()!=NFD_OKAY){g_save_manager_status="The system file picker could not be opened.";return false;}
    nfdu8char_t* result=nullptr;const nfdfilteritem_t filters[]={{"DKR Adventure save","bin,eep"}};
    const auto dialog=NFD_OpenDialogU8(&result,filters,1,nullptr);
    if(dialog==NFD_OKAY){g_shared_save_import={std::filesystem::u8path(result),0,0};NFD_FreePathU8(result);}
    else if(dialog==NFD_ERROR)g_save_manager_status=NFD_GetError();
    NFD_Quit();return dialog==NFD_OKAY;
}
