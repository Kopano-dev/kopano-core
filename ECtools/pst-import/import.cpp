/*
 * Copyright 2005 - 2016  Zarafa B.V. and its licensors
 */

#include <kopano/platform.h>

#include <iostream>
#include <kopano/ECLogger.h>
#include <libpff/error.h>
#include <libpff.h>
#include <kopano/charset/convert.h>

#include <kopano/mapi_ptr.h>
#include <mapiguid.h>
#include <mapidefs.h>
#include <mapitags.h>
#include <mapiutil.h>
#include <mapix.h>

#include <kopano/ecversion.h>
#include <kopano/CommonUtil.h>

using namespace std;

std::wstring GetPropString(libpff_item_t *item, unsigned int ulPropTag);
HRESULT value_to_spropval(uint32_t propid, uint32_t proptype, uint8_t *data, size_t size, void *base, LPSPropValue lpProp);
HRESULT item_to_srowset(libpff_item_t *item, LPSRowSet *lppRowSet);
HRESULT dump_message( libpff_item_t *item, IMAPIFolder *lpFolder);
HRESULT dump_folder(libpff_file_t *file, libpff_item_t *item, IMAPIFolder *lpFolder);
HRESULT dump_store(libpff_file_t *file, libpff_item_t *store, libpff_item_t *item, IMAPIFolder *lpFolder);


std::wstring GetPropString(libpff_item_t *item, unsigned int ulPropTag)
{
    libpff_error_t *error = NULL;
    uint32_t type = 0;
    uint8_t *data = NULL;
    size_t size;

    if(libpff_item_get_entry_value(item, 0, PROP_ID(ulPropTag), &type, &data, &size, LIBPFF_ENTRY_VALUE_FLAG_MATCH_ANY_VALUE_TYPE, &error) != 1) {
        return std::wstring(L"");
    }
    
    if(type == PT_STRING8)
        return convert_to<std::wstring>((char *)data, size, "windows-1252");
    else
        return convert_to<std::wstring>((char *)data, size, "UCS-2");
}

HRESULT value_to_spropval(uint32_t propid, uint32_t proptype, uint8_t *data, size_t size, void *base, LPSPropValue lpProp)
{
    HRESULT hr = hrSuccess;
    
    lpProp->ulPropTag = PROP_TAG(proptype, propid);
    
    switch(proptype) {
        case PT_STRING8: {
            std::string str = convert_to<std::string>("//FORCE", (char *)data, size, "windows-1252");
            
            if ((hr = MAPIAllocateMore(str.size()+1, base, (void **)&lpProp->Value.lpszA)) != hrSuccess)
		goto exit;
            strcpy(lpProp->Value.lpszA, str.c_str());
            break;
        }
        case PT_UNICODE: {
            std::wstring wstr;
            
            wstr = convert_to<std::wstring>("WCHAR_T//FORCE",(char *)data, size, "UCS-2LE");
            
            if ((hr = MAPIAllocateMore((wstr.size()+1) * sizeof(wchar_t), base, (void **)&lpProp->Value.lpszW)) != hrSuccess)
		goto exit;
            wcscpy(lpProp->Value.lpszW, wstr.c_str());
            break;
        }
        case PT_BINARY:
            if ((hr = MAPIAllocateMore(size, base, (void **)&lpProp->Value.bin.lpb)) != hrSuccess)
		goto exit;
            memcpy(lpProp->Value.bin.lpb, data, size);
            lpProp->Value.bin.cb = size;
            break;
        case PT_LONG:
            if(size != sizeof(unsigned int)) {
                wcerr << "Unexpected data size " << size << " != " << sizeof(unsigned int) << std::endl;
                hr = MAPI_E_INVALID_PARAMETER;
                goto exit;
            }
            lpProp->Value.ul = *(unsigned int *)data;
            break;
        case PT_BOOLEAN:
            if(size != sizeof(unsigned char)) {
                wcerr << "Unexpected data size " << size << " != " << sizeof(unsigned char) << std::endl;
                hr = MAPI_E_INVALID_PARAMETER;
                goto exit;
            }
            lpProp->Value.b = *(unsigned char *)data;
            break;
        case PT_I2:
            if(size != sizeof(unsigned short)) {
                wcerr << "Unexpected data size " << size << " != " << sizeof(unsigned short) << std::endl;
                hr = MAPI_E_INVALID_PARAMETER;
                goto exit;
            }
            lpProp->Value.i = *(unsigned short *)data;
            break;
        case PT_SYSTIME:
            if(size != sizeof(FILETIME)) {
                wcerr << "Unexpected data size " << size << " != " << sizeof(FILETIME) << std::endl;
                hr = MAPI_E_INVALID_PARAMETER;
                goto exit;
            }
            lpProp->Value.ft = *(FILETIME *)data;
            break;
        default:
            hr = MAPI_E_INVALID_PARAMETER;
    }

exit:
    return hr;
}

HRESULT item_to_srowset(libpff_item_t *item, LPSRowSet *lppRowSet) {
    HRESULT hr = hrSuccess;
    libpff_name_to_id_map_entry_t *name_to_id_map_entry = NULL;
    size_t size = 0;
    uint32_t entry_type = 0;
    uint32_t value_type = 0;
    uint8_t *value_data = NULL;
    uint32_t rows, columns;
    LPSRowSet rowset = NULL;
    libpff_error_t *error = NULL;
    int n = 0;
    
    if(libpff_item_get_number_of_sets(item, &rows, &error) != 1) {
        wcerr << "Unable to get number of sets\n";
        hr = MAPI_E_NOT_FOUND;
        goto exit;
    }
    
    if(libpff_item_get_number_of_entries(item, &columns, &error) != 1) {
        wcerr << "Unable to get number of entries\n";
        hr = MAPI_E_NOT_FOUND;
        goto exit;
    }

	if ((hr = MAPIAllocateBuffer(CbNewSRowSet(rows), (void **)&rowset)) != hrSuccess)
		goto exit;

	memset(rowset, 0, CbNewSRowSet(rows));
    
    for(uint32_t i = 0; i < rows; i++) {
        if ((hr = MAPIAllocateBuffer(sizeof(SPropValue) * columns, (void **)&rowset->aRow[i].lpProps)) != hrSuccess)
		goto exit;
        memset(rowset->aRow[i].lpProps, 0, sizeof(SPropValue) * columns);
        n = 0;
        for(uint32_t j = 0; j < columns; j++) {
            if(libpff_item_get_entry_type(item, i, j, &entry_type, &value_type, &name_to_id_map_entry, &error) != 1) {
                wcerr << "Unable to get property tag " << j << endl;
                continue;
            }
            
            if(libpff_item_get_entry_value(item, i, entry_type, &value_type, &value_data, &size, LIBPFF_ENTRY_VALUE_FLAG_MATCH_ANY_VALUE_TYPE | LIBPFF_ENTRY_VALUE_FLAG_IGNORE_NAME_TO_ID_MAP,  &error) == -1) {
                wcerr << "Unable to get property " << j << endl;
                continue;
            } else {
                if(!value_data)
                    continue;
                    
                if(value_to_spropval(entry_type, value_type, value_data, size, rowset->aRow[i].lpProps, &rowset->aRow[i].lpProps[n]) != hrSuccess) {
                    continue;
                }
                n++;
            }
        }
        rowset->aRow[i].cValues = n;
    }
    
    rowset->cRows = rows;

    *lppRowSet = rowset;
    
exit:    
    return hr;
}

HRESULT dump_message( libpff_item_t *item, IMAPIFolder *lpFolder) {
    HRESULT hr = hrSuccess;
    MessagePtr lpMessage;
    
    IAttach *lpAttach = NULL;
    IStream *lpStream = NULL;
    ULONG ulAttachId = 0;
    int attachcount;
    libpff_error_t *error = NULL;
    libpff_item_t *recipients    = NULL;
    mapi_rowset_ptr<SRow> props, recipprops, attachprops;
    libpff_item_t *attachment = NULL, *attachments = NULL;
    
    hr = item_to_srowset(item, &props);
    if(hr != hrSuccess) {
        wcerr << "Unable to get properties\n";
        goto exit;
    }
    
    if(props.size() != 1) {
        wcerr << "Number of sets is not 1\n";
        hr = MAPI_E_NOT_FOUND;
        goto exit;
    }
    
    if(libpff_message_get_recipients(item, &recipients, &error) != 1) {
        wcerr << "Unable to get recipients\n";
        hr = MAPI_E_NOT_FOUND;
        goto exit;
    }
    
    hr = item_to_srowset(recipients, &recipprops);
    if(hr != hrSuccess) {
        wcerr << "Unable to get recipient propers\n";
        goto exit;
    }
    
    hr = lpFolder->CreateMessage(&IID_IMessage, 0, &lpMessage);
    if(hr != hrSuccess) {
        wcerr << "Unable to create MAPI message\n";
        goto exit;
    }
        
    hr = lpMessage->SetProps(props[0].cValues, props[0].lpProps, NULL);
    if(hr != hrSuccess) {
        wcerr << "Unable to set properties\n";
        goto exit;
    }
    
    hr = lpMessage->ModifyRecipients(0, (LPADRLIST)recipprops.get());
    if(hr != hrSuccess) {
        wcerr << "Unable to set recipients\n";
        goto exit;
    }
    
    if(libpff_message_get_number_of_attachments(item, &attachcount, &error) != 1) {
        hr = MAPI_E_NOT_FOUND;
        wcerr << "Unable to get number of attachments\n";
        goto exit;
    }
    
    if(attachcount > 0) {
        if(libpff_message_get_attachments(item, &attachments, &error) != 1) {
            hr = MAPI_E_NOT_FOUND;
            wcerr << "Unable to get attachments\n";
            goto exit;
        }

        hr = item_to_srowset(attachments, &attachprops);
        if(hr != hrSuccess) {
            wcerr << "Unable to get attachment properties\n";
            goto exit;
        }
        
        for(int i = 0; i < attachcount ; i++) {
            int attachment_type = 0;
            size64_t data_size;
            
            if(libpff_message_get_attachment(item, i, &attachment, &error) != 1) {
                wcerr << "Unable to get attachment " << i << endl;
                continue;
            }
            
            hr = lpMessage->CreateAttach(&IID_IAttachment, 0, &ulAttachId, &lpAttach);
            if(hr != hrSuccess) {
                wcerr << "Unable to create attachment " << i << endl;
                goto exit;
            }
            
            hr = lpAttach->SetProps(attachprops[i].cValues, attachprops[i].lpProps, NULL);
            if(hr != hrSuccess) {
                wcerr << "Unable to set attachment properties";
                goto exit;
            }

            if(libpff_attachment_get_type(attachment, &attachment_type, &error) != 1) {
                wcerr << "Unable to get attachment type\n";
                hr = MAPI_E_NOT_FOUND;
                goto exit;
            }
            
            if(attachment_type == LIBPFF_ATTACHMENT_TYPE_DATA) {
                char buffer[65536];
                SPropValue sProp;
                
                sProp.ulPropTag = PR_ATTACH_METHOD;
                sProp.Value.ul = ATTACH_BY_VALUE;
                HrSetOneProp(lpAttach, &sProp);
                
                
                hr = lpAttach->OpenProperty(PR_ATTACH_DATA_BIN, &IID_IStream, STGM_WRITE | STGM_SHARE_EXCLUSIVE, MAPI_CREATE | MAPI_MODIFY, (IUnknown **)&lpStream);
                if(hr != hrSuccess) {
                    wcerr << "Unable to create attachment data stream\n";
                    goto exit;
                }
                
                if(libpff_attachment_get_data_size(attachment, &data_size, &error) != 1) {
                    wcerr << "Unable to get attachment size\n";
                    continue;
                }
                
                if(data_size) {
                    if(libpff_attachment_data_seek_offset(attachment, 0, SEEK_SET, &error) != 0) {
                        wcerr << "Unable to seek to attachment data start\n";
                        continue;
                    }
                    
                    while( data_size > 0 ) {
                        int read = libpff_attachment_data_read_buffer(attachment, (uint8_t *)buffer, sizeof(buffer) > data_size ? data_size : sizeof(buffer), &error);

                        if(read < 0 ) {
                            wcerr << "Read failed on data stream\n";
                            continue;
                        }

                        data_size -= read;
                        
                        hr = lpStream->Write(buffer, read, NULL);  
                        if(hr != hrSuccess)
                            goto exit;
                    }
                }
                
                lpStream->Commit(0);
                lpStream->Release();
                lpStream = NULL;
            }
            
            hr = lpAttach->SaveChanges(0);
            if(hr != hrSuccess) {
                wcerr << "Unable to save attachment\n";
                goto exit;
            }
            
            lpAttach->Release();
            lpAttach = NULL;
            
            libpff_item_free(&attachment, &error);
        }
    }
        
    hr = lpMessage->SaveChanges(0);
    if(hr != hrSuccess) {
        wcerr << "Unable to save MAPI message\n";
        goto exit;
    }
            
exit:
    if(attachments)
        libpff_item_free(&attachments, &error);
        
    if(recipients)
        libpff_item_free(&recipients, &error);
        
    return hr;
}

HRESULT dump_folder(libpff_file_t *file, libpff_item_t *item, IMAPIFolder *lpFolder) {
    HRESULT hr = hrSuccess;
    int folders, messages;
    libpff_error_t *error = NULL;
    std::wstring wstrDisplay;
    libpff_item_t *message = NULL, *subfolder = NULL;
    IMAPIFolder *lpSubFolder = NULL;

    wstrDisplay = GetPropString(item, PR_DISPLAY_NAME_A);
    
    wcerr << "Processing folder " << wstrDisplay << endl;
    
    hr = lpFolder->CreateFolder(FOLDER_GENERIC, (TCHAR *)wstrDisplay.c_str(), (TCHAR *)L"", &IID_IMAPIFolder, MAPI_UNICODE | OPEN_IF_EXISTS, &lpSubFolder);
    if(hr != hrSuccess) {
        wcerr << "Unable to create folder '" << wstrDisplay << "'\n";
        goto exit;
    }

    if(libpff_folder_get_number_of_sub_messages(item, &messages, &error) != 1) {
        wcerr << "Unable to get number of subfolders\n";
        hr = MAPI_E_NOT_FOUND;
        goto exit;
    }
    
    for(int i = 0; i < messages; i++) {
        if(libpff_folder_get_sub_message(item, i, &message, &error) != 1) {
            wcerr << "Unable to get message number " << i << endl;
            continue;
        }
        
        dump_message(message, lpSubFolder);
        
        libpff_item_free(&message, &error);
    }
    

    if(libpff_folder_get_number_of_sub_folders(item, &folders, &error) != 1) {
        hr = MAPI_E_NOT_FOUND;
        wcerr << "Unable to get number of subfolders\n";
        goto exit;
    }

    for(int i = 0; i < folders; i++) {
        if(libpff_folder_get_sub_folder(item, i, &subfolder, &error) != 1) {
            wcerr << "Unable to get folder number " << i << endl;
            continue;
        }
        
        dump_folder(file, subfolder, lpSubFolder);
        
        libpff_item_free(&subfolder, &error);
    }
    
exit:
    if (lpSubFolder)
        lpSubFolder->Release();
        
    return hr;
}

HRESULT dump_store(libpff_file_t *file, libpff_item_t *store, libpff_item_t *item, IMAPIFolder *lpFolder)
{
    HRESULT hr = hrSuccess;
    libpff_item_t *subfolder = NULL;
    uint32_t type = PROP_TYPE(PR_IPM_SUBTREE_ENTRYID);
    libpff_error_t *error = NULL;
    uint8_t *data = NULL;
    size_t size;
    mapi_rowset_ptr<SRow> r;

    std::wstring strStore = GetPropString(store, PR_DISPLAY_NAME_W);
    
    wcerr << "Store name " << strStore << endl;
    
    if(libpff_item_get_entry_value(store, 0, PROP_ID(PR_IPM_SUBTREE_ENTRYID), &type, &data, &size, 0, &error) != 1) {
        wcerr << "Unable to get ipm subtree\n";
        hr = MAPI_E_NOT_FOUND;
        goto exit;
    }
    
    if(size != 4 + sizeof(GUID) + sizeof(unsigned int)) {
        wcerr << "Unexpected entryid size for IPM subtree:" << size << endl;
        hr = MAPI_E_NOT_FOUND;
        goto exit;
    }
    
    if(libpff_file_get_item_by_identifier(file, *(unsigned int *)(data + 4 + sizeof(GUID)), &subfolder, &error) != 1) {
        wcerr << "Unable to get IPM subtree\n";
        hr = MAPI_E_NOT_FOUND;
        goto exit;
    }
    
    dump_folder(file, subfolder, lpFolder);
    
    libpff_item_free(&subfolder, &error);

exit:
    return hr; 
}

int main(int argc, char *argv[]) {
    libpff_file_t *file = NULL;
    libpff_error_t *error                              = NULL;
    libpff_item_t *pff_root_item = NULL;
    libpff_item_t *pff_store_item = NULL;
    
    char *filename;
    char *username;
    
    ULONG ulType = 0;
    HRESULT hr = hrSuccess;
    
    setlocale(LC_CTYPE, "");
    
    if(argc != 3) {
        wcerr << "Usage: pstexport <filename> <user>\n";
        return 1;
    }
    
    filename = argv[1];
    username = argv[2];

    hr = MAPIInitialize(NULL);
    if(hr != hrSuccess) {
        wcerr << "Unable to initialize MAPI\n";
        return 1;
    }

    {
        MAPISessionPtr lpSession;
        MsgStorePtr lpStore;
        MAPIFolderPtr lpFolder;
        SPropValuePtr lpPropIPM;
        
        if(libpff_file_initialize(&file, &error) != 1) {
            wcerr << "Init failed\n";
            return 1;
        }
        
        if(libpff_file_open(file, filename, LIBPFF_OPEN_READ, &error) != 1) {
            wcerr << "Unable to open file\n";
            return 1;
        }

        hr = HrOpenECSession(ec_log_get(), &lpSession, "PST importer", PROJECT_SVN_REV_STR, convert_to<std::wstring>(username).c_str(), L"", "default:");
        if(hr != hrSuccess) {
            wcerr << "Unable to open MAPI session\n";
            return 1;
        }

        if(libpff_file_get_message_store(file, &pff_store_item, &error) != 1) {
            wcerr << "Unable to get store item\n";
            hr = MAPI_E_NOT_FOUND;
            goto exit;
        }

        if(libpff_file_get_root_folder(file, &pff_root_item, &error) != 1) {
            wcerr << "Unable to get root folder\n";
            hr = MAPI_E_NOT_FOUND;
            goto exit;
        }
        
        hr = HrOpenDefaultStore(lpSession, &lpStore);
        if(hr != hrSuccess) {
            wcerr << "Unable to get default store\n";
            goto exit;
        }
        
        hr = HrGetOneProp(lpStore, PR_IPM_SUBTREE_ENTRYID, &lpPropIPM);
        if(hr != hrSuccess) {
            wcerr << "Unable to get ipm subtree root\n";
            goto exit;
        }
        
        hr = lpStore->OpenEntry(lpPropIPM->Value.bin.cb, (LPENTRYID)lpPropIPM->Value.bin.lpb, NULL, MAPI_MODIFY, &ulType, (IUnknown **)&lpFolder);
        if(hr != hrSuccess) {
            wcerr << "Unable to open ipm subtree\n";
            goto exit;
        }
        
        hr = dump_store(file, pff_store_item, pff_root_item, lpFolder);
        if(hr != hrSuccess) {
            wcerr << "Import failed\n";
            goto exit;
        }
        
        wcerr << "Import complete\n";
        
    exit:
        if(pff_root_item)
            libpff_item_free(&pff_root_item, &error);
            
        if(pff_store_item)
            libpff_item_free(&pff_store_item, &error);
            
        if(file) {
            libpff_file_close(file, &error);
            libpff_file_free(&file, &error);
        }

    }
    MAPIUninitialize();

    return hr == hrSuccess ? 0 : 2;
}
