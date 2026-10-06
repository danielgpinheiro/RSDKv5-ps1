#include "RSDK/Core/RetroEngine.hpp"

#if RETRO_REV02

// ====================
// API Cores
// ====================

namespace RSDK
{
namespace SKU
{

// Dummy API
#if RETRO_USERCORE_DUMMY
#include "RSDK/User/Dummy/DummyStorage.cpp"
#endif

// Steam API
#if RETRO_USERCORE_STEAM
#include "RSDK/User/Steam/SteamStorage.cpp"
#endif

// Epic Games API
#if RETRO_USERCORE_EOS
#include "RSDK/User/EOS/EOSStorage.cpp"
#endif

// Switch API
#if RETRO_USERCORE_NX
#include "RSDK/User/NX/NXStorage.cpp"
#endif

} // namespace SKU
} // namespace RSDK

using namespace RSDK;

#if RETRO_PLATFORM == RETRO_PS1
volatile uint32 g_ps1UserDBOverflow = 0; // 4.2a: tables / files beyond RETRO_USERDB_COL_MAX / VALUE_MAX (GDB; must stay 0)
#endif

RSDK::SKU::UserStorage *RSDK::SKU::userStorage     = NULL;
RSDK::SKU::UserDBStorage *RSDK::SKU::userDBStorage = NULL;

// =======================
// USER DB VALUE
// =======================
bool32 RSDK::SKU::UserDBValue::CheckMatch(int32 row, int32 column)
{
    UserDB *userDB     = (UserDB *)parent;
    UserDBValue *other = &userDB->rows[row].values[column];

    switch (userDB->columnTypes[column]) {
        case DBVAR_BOOL:
        case DBVAR_UINT8:
        case DBVAR_INT8: return memcmp(data, other->data, sizeof(int8)) == 0;

        case DBVAR_UINT16:
        case DBVAR_INT16: return memcmp(data, other->data, sizeof(int16)) == 0;

        case DBVAR_UINT32:
        case DBVAR_INT32:
        case DBVAR_FLOAT:
        case DBVAR_COLOR: return memcmp(data, other->data, sizeof(int32)) == 0;

        case DBVAR_STRING: {
            char *thisString  = (char *)data;
            char *otherString = (char *)other->data;

            return strcmp(thisString, otherString) == 0;
        }
        default: break;
    }

    return false;
}
void RSDK::SKU::UserDBValue::Set(int32 type, void *data)
{
    size = 0;
    memset(this->data, 0, sizeof(this->data));
    if (data) {
        switch (type) {
            case DBVAR_BOOL:
            case DBVAR_UINT8:
            case DBVAR_INT8:
                size = sizeof(int8);
                memcpy(this->data, data, sizeof(int8));
                break;

            case DBVAR_UINT16:
            case DBVAR_INT16:
                size = sizeof(int16);
                memcpy(this->data, data, sizeof(int16));
                break;

            case DBVAR_UINT32:
            case DBVAR_INT32:
            case DBVAR_FLOAT:
            case DBVAR_COLOR:
                size = sizeof(int32);
                memcpy(this->data, data, sizeof(int32));
                break;

            case DBVAR_STRING: {
                char *string = (char *)data;

                int32 len = (int32)strlen(string);
                for (int32 c = 0; c < len && c < (int32)sizeof(this->data) - 1; ++c) {
                    this->data[c] = string[c];
                }
                break;
            }

            default: break;
        }
    }
}
void RSDK::SKU::UserDBValue::Get(int32 type, void *data)
{
    int8 *outData = (int8 *)data;
    switch (type) {
        case DBVAR_BOOL:
        case DBVAR_UINT8:
        case DBVAR_INT8: memcpy(outData, this->data, sizeof(int8)); break;

        case DBVAR_UINT16:
        case DBVAR_INT16: memcpy(outData, this->data, sizeof(int16)); break;

        case DBVAR_UINT32:
        case DBVAR_INT32:
        case DBVAR_FLOAT:
        case DBVAR_COLOR: memcpy(outData, this->data, sizeof(int32)); break;

        case DBVAR_STRING: {
            memset(outData, 0, size + 1);
            char *string = (char *)this->data;
            for (int32 c = 0; c < size; ++c) {
                outData[c] = string[c];
            }
            break;
        }

        default: break;
    }
}

// =======================
// USER DB ROW
// =======================
bool32 RSDK::SKU::UserDBRow::AddValue(int32 type, char *name, void *value)
{
    UserDB *userDB = parent;
    uint32 uuid    = 0;
    GenerateHashCRC(&uuid, name);

    for (int32 c = 0; c < userDB->columnCount; ++c) {
        if (userDB->columnUUIDs[c] == uuid) {
            if (c < 0 || type != userDB->columnTypes[c])
                return false;

            values[c].Set(type, value);

            time_t t;
            time(&t);
            changeTime = *localtime(&t);
            return true;
        }
    }

    return false;
}
bool32 RSDK::SKU::UserDBRow::GetValue(int32 type, char *name, void *value)
{
    UserDB *userDB = parent;
    uint32 uuid    = 0;
    GenerateHashCRC(&uuid, name);

    for (int32 c = 0; c < userDB->columnCount; ++c) {
        if (userDB->columnUUIDs[c] == uuid) {
            if (c < 0 || type != userDB->columnTypes[c])
                return false;

            values[c].Get(type, value);
            return true;
        }
    }

    return false;
}

bool32 RSDK::SKU::UserDBRow::Compare(UserDBRow *other, int32 type, char *name, bool32 sortAscending)
{
    uint8 thisData[0x10];
    uint8 otherData[0x10];
    memset(thisData, 0, sizeof(thisData));
    memset(otherData, 0, sizeof(otherData));

    this->GetValue(type, name, thisData);
    other->GetValue(type, name, otherData);
    switch (type) {
        case DBVAR_BOOL:
        case DBVAR_UINT8: {
            uint8 thisValue = 0, otherValue = 0;
            memcpy(&thisValue, thisData, sizeof(uint8));
            memcpy(&otherValue, otherData, sizeof(uint8));

            if (!sortAscending)
                return thisValue > otherValue;
            else
                return thisValue < otherValue;
            break;
        }

        case DBVAR_INT8: {
            int8 thisValue = 0, otherValue = 0;
            memcpy(&thisValue, thisData, sizeof(int8));
            memcpy(&otherValue, otherData, sizeof(int8));

            if (!sortAscending)
                return thisValue > otherValue;
            else
                return thisValue < otherValue;
            break;
        }

        case DBVAR_UINT16: {
            uint16 thisValue = 0, otherValue = 0;
            memcpy(&thisValue, thisData, sizeof(uint16));
            memcpy(&otherValue, otherData, sizeof(uint16));

            if (!sortAscending)
                return thisValue > otherValue;
            else
                return thisValue < otherValue;
            break;
        }

        case DBVAR_INT16: {
            int16 thisValue = 0, otherValue = 0;
            memcpy(&thisValue, thisData, sizeof(int16));
            memcpy(&otherValue, otherData, sizeof(int16));

            if (!sortAscending)
                return thisValue > otherValue;
            else
                return thisValue < otherValue;
            break;
        }

        case DBVAR_UINT32:
        case DBVAR_COLOR: {
            uint32 thisValue = 0, otherValue = 0;
            memcpy(&thisValue, thisData, sizeof(uint32));
            memcpy(&otherValue, otherData, sizeof(uint32));

            if (!sortAscending)
                return thisValue > otherValue;
            else
                return thisValue < otherValue;
            break;
        }

        case DBVAR_INT32: {
            int32 thisValue = 0, otherValue = 0;
            memcpy(&thisValue, thisData, sizeof(int32));
            memcpy(&otherValue, otherData, sizeof(int32));

            if (!sortAscending)
                return thisValue > otherValue;
            else
                return thisValue < otherValue;
            break;
        }

        case DBVAR_FLOAT: {
            float thisValue = 0, otherValue = 0;
            memcpy(&thisValue, thisData, sizeof(float));
            memcpy(&otherValue, otherData, sizeof(float));

            if (!sortAscending)
                return thisValue > otherValue;
            else
                return thisValue < otherValue;
            break;
        }

        case DBVAR_STRING: {
            char *thisString  = (char *)thisData;
            char *otherString = (char *)otherData;

            int32 result = strcmp(thisString, otherString);
            if (sortAscending)
                return result > 0 ? true : false;
            else
                return result < 0 ? true : false;
            break;
        }
        default: break;
    }
    return false;
}

// =======================
// USER DB
// =======================
void RSDK::SKU::UserDB::Init(va_list list)
{
    int32 cnt = 0;
    while (true) {
        int32 type = va_arg(list, int32);
        if (!type)
            break;
#if RETRO_PLATFORM == RETRO_PS1
        if (cnt >= RETRO_USERDB_COL_MAX) { // 4.2a: the stock trusts every table to have at most 8
            g_ps1UserDBOverflow = g_ps1UserDBOverflow + 1;
            break;
        }
#endif

        columnTypes[cnt] = type;
        memset(columnNames[cnt], 0, 0x10);
        sprintf_s(columnNames[cnt], sizeof(columnNames[cnt]), "%s", va_arg(list, const char *));
        GenerateHashCRC(&columnUUIDs[cnt], columnNames[cnt]);
        ++cnt;
    }

    columnCount = cnt;
    rowCount    = 0;
    memset(rows, 0, sizeof(rows));
    Refresh();
    active = true;
    valid  = true;
}
// docs/32 9b.5: a table's rows are packed (1-byte value sizes, 16-byte names, tm structs), so a row's uuid sits at any
// address: the R3000 raises an address error on an unaligned word access (the first Time Attack record crashed the save).
#if RETRO_PLATFORM == RETRO_PS1
template <typename T> static inline T PS1DbGet(const uint8 *p)
{
    T v;
    memcpy(&v, p, sizeof(T));
    return v;
}
template <typename T> static inline void PS1DbPut(uint8 *p, T v) { memcpy(p, &v, sizeof(T)); }
#define PS1_DB_GET(T, p)    PS1DbGet<T>(p)
#define PS1_DB_PUT(T, p, v) PS1DbPut<T>(p, (T)(v))
#else
#define PS1_DB_GET(T, p)    (*(T *)(p))
#define PS1_DB_PUT(T, p, v) (*(T *)(p) = (v))
#endif
bool32 RSDK::SKU::UserDB::Load(uint8 *buffer)
{
    uint32 signature = PS1_DB_GET(uint32, buffer);
    if (signature != RETRO_USERDB_SIGNATURE)
        return false;

    buffer += sizeof(int32);
    buffer += sizeof(int32); // used size

    rowCount = PS1_DB_GET(uint16, buffer);
    buffer += sizeof(uint16);

    columnCount = *buffer;
    buffer++;
#if RETRO_PLATFORM == RETRO_PS1
    if (columnCount > RETRO_USERDB_COL_MAX) { // 4.2a: a file this table cannot hold: refused (the game makes a new one)
        g_ps1UserDBOverflow = g_ps1UserDBOverflow + 1;
        columnCount         = 0;
        return false;
    }
#endif

    for (int32 c = 0; c < columnCount; ++c) {
        columnTypes[c] = *buffer;
        buffer++;

        sprintf_s(columnNames[c], sizeof(columnNames[c]), "%s", (char *)buffer);
        buffer += 0x10;

        GenerateHashCRC(&columnUUIDs[c], columnNames[c]);
    }

    for (int32 r = 0; r < rowCount; ++r) {
        rows[r].uuid = PS1_DB_GET(uint32, buffer);
        buffer += sizeof(uint32);

        memcpy(&rows[r].createTime, buffer, sizeof(tm));
        buffer += sizeof(tm);
        memcpy(&rows[r].changeTime, buffer, sizeof(tm));
        buffer += sizeof(tm);

        for (int32 c = 0; c < columnCount; ++c) {
            rows[r].values[c].size = *buffer;
            buffer++;
#if RETRO_PLATFORM == RETRO_PS1
            if (rows[r].values[c].size > RETRO_USERDB_VALUE_MAX) {
                g_ps1UserDBOverflow = g_ps1UserDBOverflow + 1;
                rowCount            = 0;
                return false;
            }
#endif

            memcpy(&rows[r].values[c].data, buffer, rows[r].values[c].size);
            buffer += rows[r].values[c].size;
        }
    }

    active = true;
    Refresh();
    return true;
}
void RSDK::SKU::UserDB::Save(int32 totalSize, uint8 *buffer)
{
    int32 size = 0;
    if (sizeof(uint32) < totalSize) {
        size              = sizeof(uint32);
        PS1_DB_PUT(uint32, buffer, RETRO_USERDB_SIGNATURE);
        buffer += sizeof(uint32);
    }

    if (size + sizeof(uint32) < totalSize) {
        size += sizeof(uint32);
        PS1_DB_PUT(uint32, buffer, (int32)GetSize()); // used size
        buffer += sizeof(uint32);
    }

    if (size + sizeof(uint16) < totalSize) {
        size += sizeof(uint16);
        PS1_DB_PUT(uint16, buffer, rowCount);
        buffer += sizeof(uint16);
    }

    if (size + sizeof(uint8) < totalSize) {
        ++size;
        *buffer++ = columnCount;
    }

    for (int32 c = 0; c < columnCount; ++c) {
        if (size + sizeof(uint8) < totalSize) {
            ++size;
            *buffer++ = (uint8)columnTypes[c];
        }
        if (size + 0x10 * sizeof(uint8) < totalSize) {
            memset(buffer, 0, 0x10 * sizeof(uint8));
            sprintf_s((char *)buffer, sizeof(columnNames[c]), "%s", columnNames[c]);
            size += 0x10;
            buffer += 0x10;
        }
    }

    for (int32 r = 0; r < rowCount; ++r) {
        if (size + sizeof(uint32) < totalSize) {
            size += sizeof(uint32);
            PS1_DB_PUT(uint32, buffer, rows[r].uuid);
            buffer += sizeof(uint32);
        }
        if (size + sizeof(tm) < totalSize) {
            memcpy(buffer, &rows[r].createTime, sizeof(tm));
            size += sizeof(tm);
            buffer += sizeof(tm);
        }
        if (size + sizeof(tm) < totalSize) {
            memcpy(buffer, &rows[r].changeTime, sizeof(tm));
            size += sizeof(tm);
            buffer += sizeof(tm);
        }

        for (int32 c = 0; c < columnCount; ++c) {
            if (size + sizeof(uint8) < totalSize) {
                ++size;
                *buffer++ = (uint8)rows[r].values[c].size;
            }
            if (rows[r].values[c].size + size < totalSize) {
                memcpy(buffer, rows[r].values[c].data, rows[r].values[c].size);
                size += rows[r].values[c].size;
                buffer += rows[r].values[c].size;
            }
        }
    }

    if (size < totalSize)
        memset(buffer, 0, totalSize - size);
}
void RSDK::SKU::UserDB::Clear()
{
    sortedRowList.Clear();
    loaded      = false;
    active      = false;
    valid       = false;
    uuid        = 0;
    rowCount    = 0;
    columnCount = 0;
    memset(columnTypes, 0, sizeof(columnTypes));
    memset(columnNames, 0, sizeof(columnNames));
    memset(columnUUIDs, 0, sizeof(columnUUIDs));
    memset(rows, 0, sizeof(rows));
    rowsChanged = true;
}

int32 RSDK::SKU::UserDB::GetColumnID(const char *name)
{
    uint32 uuid = 0;
    GenerateHashCRC(&uuid, (char *)name);

    int32 id = -1;
    for (int32 i = 0; i < columnCount; ++i) {
        if (uuid == columnUUIDs[i]) {
            id = i;
            break;
        }
    }
    return id;
}

int32 RSDK::SKU::UserDB::AddRow()
{
    if (rowCount >= RETRO_USERDB_ROW_MAX)
        return -1;

    UserDBRow *row = &rows[rowCount];
    row->uuid      = CreateRowUUID();

    time_t timeInfo;
    time(&timeInfo);
    tm *tmA = localtime(&timeInfo);
    tm *tmB = localtime(&timeInfo);

    memcpy(&row->createTime, tmA, sizeof(tm));
    memcpy(&row->changeTime, tmB, sizeof(tm));

    memset(row->values, 0, sizeof(UserDBValue) * RETRO_USERDB_COL_MAX);
    ++rowCount;
    valid = true;

    Refresh();
    rowsChanged = true;
    return rowCount - 1;
}
bool32 RSDK::SKU::UserDB::RemoveRow(uint32 row)
{
    if (row < rowCount) {
        UserDBRow *entry = &rows[row];
        memset(entry, 0, sizeof(UserDBRow));

        int32 id = (int32)(rowCount - row - 1);
        memcpy(entry, &entry[1], id * sizeof(UserDBRow));
        memset(&entry[id + 1], 0, sizeof(UserDBRow));

        --rowCount;
        valid = true;

        Refresh();
        rowsChanged = true;
    }
    return true;
}
bool32 RSDK::SKU::UserDB::RemoveAllRows()
{
    rowCount = 0;
    memset(rows, 0, sizeof(UserDBRow) * RETRO_USERDB_ROW_MAX);
    Refresh();
    return true;
}
uint16 RSDK::SKU::UserDB::GetRowByID(uint32 uuid)
{
    if (!rowCount)
        return -1;

    for (int32 i = 0; i < rowCount; ++i) {
        if (uuid == rows[i].uuid) {
            return i;
        }
    }
    return -1;
}

void RSDK::SKU::UserDB::FilterValues(UserDBValue *value, int32 column)
{
    for (int32 i = sortedRowList.Count() - 1; i >= 0; --i) {
        if (!value->CheckMatch(sortedRowIDs[i], column)) {
            sortedRowList.Remove(i);
        }
    }
}
void RSDK::SKU::UserDB::AddSortFilter(const char *name, void *value)
{
    int32 id = GetColumnID(name);

    if (id >= 0) {
        if (sortedRowCount) {
            UserDBValue dbValue;
            // this is very hacky
            dbValue.parent = (UserDBRow *)this;
            dbValue.Set(columnTypes[id], value);
            FilterValues(&dbValue, id);
            SetupRowSortIDs();
        }
    }
}

void RSDK::SKU::UserDB::SetupRowSortIDs()
{
    sortedRowCount = 0;
    memset(sortedRowIDs, 0, sizeof(sortedRowIDs));

    for (int32 i = 0; i < sortedRowList.Count(); ++i) {
        sortedRowIDs[i] = *sortedRowList.At(i);
        ++sortedRowCount;
    }
}
void RSDK::SKU::UserDB::RefreshSortList()
{
    sortedRowList.Clear();

    for (int32 i = 0; i < rowCount; ++i) {
        int32 *row = sortedRowList.Append();
        if (row)
            *row = i;
    }

    rowsChanged = false;

    SetupRowSortIDs();
}
void RSDK::SKU::UserDB::SortRows(int32 type, char *name, bool32 sortAscending)
{
    if (!rowsChanged && sortedRowCount) {
        if (type || name) { // sort by value
            int32 col = GetColumnID(name);
            if (col >= 0) {
                for (int32 i = 0; i < sortedRowList.Count(); i++) {
                    for (int32 j = i + 1; j < sortedRowList.Count(); j++) {
                        int32 *id1 = sortedRowList.At(i);
                        int32 *id2 = sortedRowList.At(j);

                        if (rows[*id1].Compare(&rows[*id2], type, name, sortAscending)) {
                            int32 temp = *id1;
                            *id1       = *id2;
                            *id2       = temp;
                        }
                    }
                }

                SetupRowSortIDs();
            }
        }
        else { // sort by date
            for (int32 i = 0; i < sortedRowList.Count(); i++) {
                for (int32 j = i + 1; j < sortedRowList.Count(); j++) {
                    int32 *id1 = sortedRowList.At(i);
                    int32 *id2 = sortedRowList.At(j);

                    double d = difftime(mktime(&rows[*id1].createTime), mktime(&rows[*id2].createTime));

                    bool32 swap = false;
                    if (sortAscending)
                        swap = (d < 0) - (d > 0);
                    else
                        swap = (d > 0) - (d < 0);

                    if (swap) {
                        int32 temp = *id1;
                        *id1       = *id2;
                        *id2       = temp;
                    }
                }
            }

            SetupRowSortIDs();
        }
    }
}

uint32 RSDK::SKU::UserDB::CreateRowUUID()
{
    bool32 flag = true;
    uint32 uuid = 0;

    while (flag) {
        uint8 bytes[4];
        bytes[0] = rand();
        bytes[1] = rand();
        bytes[2] = rand();
        bytes[3] = rand();
        uuid     = (bytes[0]) | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24);

        if (uuid < 0x10000000)
            uuid |= 0x10000000;

        flag = false;
        for (int32 e = 0; e < rowCount; ++e) {
            if (uuid == rows[e].uuid) {
                flag = true;
            }
        }
    }
    return uuid;
}
void RSDK::SKU::UserDB::Refresh()
{
    parent = this;
    for (int32 r = 0; r < RETRO_USERDB_ROW_MAX; ++r) {
        rows[r].parent = this;
        for (int32 c = 0; c < columnCount; ++c) {
            rows[r].values[c].parent = &rows[r];
        }
    }
}
size_t RSDK::SKU::UserDB::GetSize()
{
    int32 colSize = 1;
    if (columnCount)
        colSize = (0x10 * columnCount) + columnCount + 1;

    size_t size = colSize + 10;

    for (int32 r = 0; r < RETRO_USERDB_ROW_MAX; ++r) {
        rows[r].parent = this;
        int32 rowSize  = (sizeof(tm) * 2) + sizeof(uint32);
        for (int32 c = 0; c < columnCount; ++c) {
            rowSize += sizeof(uint8) + rows[r].values[c].size;
        }
        size += rowSize;
    }

    return size;
}

// =======================
// USER DB STORAGE
// =======================

RSDK::SKU::UserDBStorage::UserDBStorage()
{
    unknown = 0;

#if RETRO_PLATFORM == RETRO_PS1
    // PS1 (docs/32 2.3i): the stock sets 8 of each whatever RETRO_USERDB_MAX is; with fewer databases that wrote past the
    // object (with 2: over its own dbLoadCB..unused; with 1: into the heap's block headers)
    void (*const loadCBs[8])(int32) = { UserDBStorage_LoadCB1, UserDBStorage_LoadCB2, UserDBStorage_LoadCB3, UserDBStorage_LoadCB4,
                                        UserDBStorage_LoadCB5, UserDBStorage_LoadCB6, UserDBStorage_LoadCB7, UserDBStorage_LoadCB8 };
    void (*const saveCBs[8])(int32) = { UserDBStorage_SaveCB1, UserDBStorage_SaveCB2, UserDBStorage_SaveCB3, UserDBStorage_SaveCB4,
                                        UserDBStorage_SaveCB5, UserDBStorage_SaveCB6, UserDBStorage_SaveCB7, UserDBStorage_SaveCB8 };
    for (int32 i = 0; i < RETRO_USERDB_MAX; ++i) {
        this->loadCallback[i] = loadCBs[i];
        this->saveCallback[i] = saveCBs[i];
    }
    return;
#endif
    this->loadCallback[0] = UserDBStorage_LoadCB1;
    this->loadCallback[1] = UserDBStorage_LoadCB2;
    this->loadCallback[2] = UserDBStorage_LoadCB3;
    this->loadCallback[3] = UserDBStorage_LoadCB4;
    this->loadCallback[4] = UserDBStorage_LoadCB5;
    this->loadCallback[5] = UserDBStorage_LoadCB6;
    this->loadCallback[6] = UserDBStorage_LoadCB7;
    this->loadCallback[7] = UserDBStorage_LoadCB8;

    this->saveCallback[0] = UserDBStorage_SaveCB1;
    this->saveCallback[1] = UserDBStorage_SaveCB2;
    this->saveCallback[2] = UserDBStorage_SaveCB3;
    this->saveCallback[3] = UserDBStorage_SaveCB4;
    this->saveCallback[4] = UserDBStorage_SaveCB5;
    this->saveCallback[5] = UserDBStorage_SaveCB6;
    this->saveCallback[6] = UserDBStorage_SaveCB7;
    this->saveCallback[7] = UserDBStorage_SaveCB8;
}

// UserDB Management
uint16 RSDK::SKU::UserDBStorage::InitUserDB(const char *name, va_list list)
{
    int32 tableID = -1;
    uint32 uuid   = 0;
    GenerateHashCRC(&uuid, (char *)name);

    for (int32 i = 0; i < RETRO_USERDB_MAX; ++i) {
        if (!userDBStorage->userDB[i].loaded) {
            tableID = i;
            break;
        }
    }

    if (tableID == -1)
        return -1;

    UserDB *userDB = &userDBStorage->userDB[tableID];

    userDB->loaded = true;
    userDB->name   = name;
    GenerateHashCRC(&userDB->uuid, (char *)name);
    userDB->Init(list);
    userDB->parent->RefreshSortList();

    return tableID;
}
uint16 RSDK::SKU::UserDBStorage::LoadUserDB(const char *filename, void (*callback)(int32 status))
{
    int32 tableID = -1;
    uint32 uuid   = 0;
    GenerateHashCRC(&uuid, (char *)filename);
    for (int32 i = 0; i < RETRO_USERDB_MAX; ++i) {
        if (uuid == userDBStorage->userDB[i].uuid && userDBStorage->userDB[i].loaded)
            return i;
    }

    for (int32 i = 0; i < RETRO_USERDB_MAX; ++i) {
        if (!userDBStorage->userDB[i].loaded) {
            tableID = i;
            break;
        }
    }

    if (tableID == -1) {
        if (callback)
            callback(STATUS_ERROR);
        return -1;
    }

#if RETRO_PLATFORM == RETRO_PS1
    // docs/32 4.2a: a buffer for the file, not the table: a saved table is at most GetSize() bytes, and the PS1's one
    // table (TimeAttackDB: 6 columns holding 12 value bytes) is 10 + (6 x 0x10 + 6 + 1) + 0xE0 rows x (2 x tm + uuid +
    // 6 sizes + 12) = 21,169 bytes; sizeof(UserDB) (62 KB) did not fit TMP (32 KB at 2 MB)
    const uint32 readSize = 10 + (0x10 * 6 + 6 + 1) + RETRO_USERDB_ROW_MAX * (2 * sizeof(tm) + sizeof(uint32) + 6 + 12);
#else
    const uint32 readSize = sizeof(UserDB);
#endif
    AllocateStorage((void **)&userDBStorage->readBuffer[tableID], readSize, DATASET_TMP, true);
    userDBStorage->dbLoadCB[tableID]  = UserDBStorage::LoadCB;
    userDBStorage->callbacks[tableID] = callback;
    TryLoadUserFile(filename, userDBStorage->readBuffer[tableID], readSize, userDBStorage->loadCallback[tableID]);

    UserDB *userDB = &userDBStorage->userDB[tableID];
    userDB->loaded = true;
    userDB->name   = filename;
    userDB->uuid   = uuid;

    return tableID;
}
bool32 RSDK::SKU::UserDBStorage::SaveUserDB(uint16 tableID, void (*callback)(int32 status))
{
    UserDB *userDB = &userDBStorage->userDB[tableID];

    bool32 success = false;
    if (userDB->active) {
        int32 totalSize = (int32)userDB->GetSize();
        AllocateStorage((void **)&userDBStorage->writeBuffer[tableID], totalSize, DATASET_TMP, true);
        userDB->Save(totalSize, (uint8 *)userDBStorage->writeBuffer[tableID]);
        userDBStorage->dbSaveCB[tableID]  = UserDBStorage::SaveCB;
        userDBStorage->callbacks[tableID] = callback;
        success = TrySaveUserFile(userDB->name, userDBStorage->writeBuffer[tableID], totalSize, userDBStorage->saveCallback[tableID], true);
    }
    else {
        if (callback)
            callback(STATUS_ERROR);
    }

    return success;
}
void RSDK::SKU::UserDBStorage::ClearUserDB(uint16 tableID)
{
    if (tableID == (uint16)-1)
        return;

    UserDB *userDB = &userDBStorage->userDB[tableID];
    if (userDB->loaded) {
        userDB->Clear();
    }
}
void RSDK::SKU::UserDBStorage::ClearAllUserDBs()
{
    for (int32 i = 0; i < RETRO_USERDB_MAX; ++i) ClearUserDB(i);
}

bool32 RSDK::SKU::UserDBStorage::LoadCB(uint16 tableID, int32 status)
{
    bool32 succeeded = false;
    if (status == STATUS_OK) {
        UserDB *userDB = &userDBStorage->userDB[tableID];

        succeeded = userDB->Load((uint8 *)userDBStorage->readBuffer[tableID]);
        if (succeeded) {
            userDB->parent->RefreshSortList();
        }
    }
    else {
        RSDK::SKU::ClearUserDB(tableID);
    }
    RemoveStorageEntry((void **)&userDBStorage->readBuffer[tableID]);

    if (userDBStorage->callbacks[tableID]) {
        userDBStorage->callbacks[tableID](succeeded ? status : STATUS_ERROR);
        userDBStorage->callbacks[tableID] = NULL;
    }

    return succeeded;
}
bool32 RSDK::SKU::UserDBStorage::SaveCB(uint16 tableID, int32 status)
{
    RemoveStorageEntry((void **)&userDBStorage->writeBuffer[tableID]);
    if (status != STATUS_OK)
        userDBStorage->userDB[tableID].valid = false;

    if (userDBStorage->callbacks[tableID]) {
        userDBStorage->callbacks[tableID](status);
        userDBStorage->callbacks[tableID] = NULL;
        return true;
    }
    return false;
}

// =======================
// USER DB API
// =======================

// UserDB Row Sorting
uint16 RSDK::SKU::SetupUserDBRowSorting(uint16 tableID)
{
    if (tableID == (uint16)-1)
        return 0;
    userDBStorage->userDB[tableID].parent->RefreshSortList();

    return userDBStorage->userDB[tableID].sortedRowCount;
}
int32 RSDK::SKU::AddUserDBRowSortFilter(uint16 tableID, int32 type, const char *name, void *value)
{
    if (tableID == (uint16)-1)
        return 0;
    UserDB *userDB = &userDBStorage->userDB[tableID];
    if (!userDB->active)
        return 0;

    userDB->parent->AddSortFilter(name, value);
    return userDB->sortedRowCount;
}
int32 RSDK::SKU::SortUserDBRows(uint16 tableID, int32 type, const char *name, bool32 sortAscending)
{
    if (tableID == (uint16)-1)
        return 0;

    UserDB *userDB = &userDBStorage->userDB[tableID];
    if (!userDB->active)
        return 0;

    userDB->parent->SortRows(type, (char *)name, sortAscending);
    return userDB->sortedRowCount;
}
int32 RSDK::SKU::GetSortedUserDBRowCount(uint16 tableID)
{
    if (tableID == (uint16)-1)
        return 0;

    UserDB *userDB = &userDBStorage->userDB[tableID];
    if (!userDB->active)
        return 0;

    return userDB->sortedRowCount;
}
int32 RSDK::SKU::GetSortedUserDBRowID(uint16 tableID, uint16 sortedRowID)
{
    if (tableID == (uint16)-1)
        return -1;

    UserDB *userDB = &userDBStorage->userDB[tableID];
    if (!userDB->active || userDB->rowsChanged || sortedRowID >= userDB->sortedRowCount)
        return -1;

    return userDB->sortedRowIDs[sortedRowID];
}

// UserDB Values
bool32 RSDK::SKU::GetUserDBValue(uint16 tableID, uint32 rowID, int32 type, char *name, void *value)
{
    if (tableID == (uint16)-1 || rowID == (uint16)-1 || !userDBStorage->userDB[tableID].active)
        return false;
    return userDBStorage->userDB[tableID].rows[rowID].GetValue(type, name, value);
}
bool32 RSDK::SKU::SetUserDBValue(uint16 tableID, uint32 rowID, int32 type, char *name, void *value)
{
    if (tableID == (uint16)-1 || rowID == (uint16)-1 || !userDBStorage->userDB[tableID].active)
        return false;

    return userDBStorage->userDB[tableID].rows[rowID].AddValue(type, name, value);
}

// UserDB Misc
bool32 RSDK::SKU::GetUserDBRowsChanged(uint16 tableID) { return userDBStorage->userDB[tableID].rowsChanged; }
void RSDK::SKU::GetUserDBRowCreationTime(uint16 tableID, uint16 rowID, char *buf, size_t size, char *format)
{
    if (tableID != (uint16)-1 && rowID != (uint16)-1) {
        UserDB *userDB = &userDBStorage->userDB[tableID];
        if (userDB->active)
            strftime(buf, size, format, &userDB->rows[rowID].createTime);
    }
}

// =======================
// USER DB CALLBACKS
// =======================

void RSDK::SKU::UserDBStorage_LoadCB1(int32 status)
{
    if (userDBStorage->dbLoadCB[0])
        userDBStorage->dbLoadCB[0](0, status);
}
void RSDK::SKU::UserDBStorage_LoadCB2(int32 status)
{
    if (userDBStorage->dbLoadCB[1])
        userDBStorage->dbLoadCB[1](1, status);
}
void RSDK::SKU::UserDBStorage_LoadCB3(int32 status)
{
    if (userDBStorage->dbLoadCB[2])
        userDBStorage->dbLoadCB[2](2, status);
}
void RSDK::SKU::UserDBStorage_LoadCB4(int32 status)
{
    if (userDBStorage->dbLoadCB[3])
        userDBStorage->dbLoadCB[3](3, status);
}
void RSDK::SKU::UserDBStorage_LoadCB5(int32 status)
{
    if (userDBStorage->dbLoadCB[4])
        userDBStorage->dbLoadCB[4](4, status);
}
void RSDK::SKU::UserDBStorage_LoadCB6(int32 status)
{
    if (userDBStorage->dbLoadCB[5])
        userDBStorage->dbLoadCB[5](5, status);
}
void RSDK::SKU::UserDBStorage_LoadCB7(int32 status)
{
    if (userDBStorage->dbLoadCB[6])
        userDBStorage->dbLoadCB[6](6, status);
}
void RSDK::SKU::UserDBStorage_LoadCB8(int32 status)
{
    if (userDBStorage->dbLoadCB[7])
        userDBStorage->dbLoadCB[7](7, status);
}

void RSDK::SKU::UserDBStorage_SaveCB1(int32 status)
{
    if (userDBStorage->dbSaveCB[0])
        userDBStorage->dbSaveCB[0](0, status);
}
void RSDK::SKU::UserDBStorage_SaveCB2(int32 status)
{
    if (userDBStorage->dbSaveCB[1])
        userDBStorage->dbSaveCB[1](1, status);
}
void RSDK::SKU::UserDBStorage_SaveCB3(int32 status)
{
    if (userDBStorage->dbSaveCB[2])
        userDBStorage->dbSaveCB[2](2, status);
}
void RSDK::SKU::UserDBStorage_SaveCB4(int32 status)
{
    if (userDBStorage->dbSaveCB[3])
        userDBStorage->dbSaveCB[3](3, status);
}
void RSDK::SKU::UserDBStorage_SaveCB5(int32 status)
{
    if (userDBStorage->dbSaveCB[4])
        userDBStorage->dbSaveCB[4](4, status);
}
void RSDK::SKU::UserDBStorage_SaveCB6(int32 status)
{
    if (userDBStorage->dbSaveCB[5])
        userDBStorage->dbSaveCB[5](5, status);
}
void RSDK::SKU::UserDBStorage_SaveCB7(int32 status)
{
    if (userDBStorage->dbSaveCB[6])
        userDBStorage->dbSaveCB[6](6, status);
}
void RSDK::SKU::UserDBStorage_SaveCB8(int32 status)
{
    if (userDBStorage->dbSaveCB[7])
        userDBStorage->dbSaveCB[7](7, status);
}

#endif

void (*RSDK::SKU::preLoadSaveFileCB)();
void (*RSDK::SKU::postLoadSaveFileCB)();
char RSDK::SKU::userFileDir[0x100];

#if RETRO_PLATFORM == RETRO_PS1
// docs/32 phase 8: Mania's user files live in one memory card file (ps1/user_card.cpp: "BAHBRW-00006MANIA", slot 1) as
// packed sections: 'MSV1', u16 sections, u16 0, then per section its name (16 chars), raw bytes, packed bytes and the
// packed words (u32 tokens: bit 31 set = that many zero words, else that many literal words follow: the save RAM is
// mostly zeros). A load reads the file and unpacks one section; a save reads the file, replaces its section, writes it
// back. The payload is staged in DATASET_TMP's tail (nothing lives there in play; no resident cache: the heap is STG's).
extern "C" uint32_t PS1UserCardRoom();
extern "C" uint32_t PS1UserCardRead(uint8_t *buf, uint32_t cap, uint32 *outLen); // (C linkage: uint32 = the same 32 bits)
extern "C" uint32_t PS1UserCardWrite(uint8_t *buf, uint32_t len);
extern "C" uint32_t PS1UserCardWriteAsync(uint8_t *buf, uint32_t len);
extern "C" void PS1UserCardWait();
extern "C" volatile uint32 g_ps1CardLastError; // (ps1/user_card.cpp: the last psyqo MemoryCard::Error)
volatile uint32 g_ps1CardLoads = 0, g_ps1CardSaves = 0, g_ps1CardErrors = 0, g_ps1CardSkipped = 0, g_ps1CardStageFail = 0,
                g_ps1CardTooBig = 0, g_ps1CardPayload = 0, g_ps1CardFresh = 0;
volatile uint32 g_ps1CardOff = 0;
volatile uint32 g_ps1CardStageInfo[5] = { 0 }; // the last staging: size, TMP used, limit, flags (1 in TMP, 2 owner, 4 compacted), used after // GDB: no card calls (loads find nothing, saves report failure)
namespace
{
#define PS1_CARD_HEADER (16)               // ps1/memcard's header in front of the payload (PS1_MC_HEADER_SIZE)
#define PS1_CARD_ROOM   (3 * 8192 - 256)   // the card file's room at most: 3 blocks less the title and icon frames
#define PS1_CARD_MAGIC  (0x3156534D)       // 'MSV1'
struct PS1CardSection {
    char name[16];
    uint32 raw, packed;
};
// the files kept (keep: bytes stored, 0 = all): SaveData = the save RAM's Mania part (slots 0-7, time attack 0x800,
// progress 0x900; Encore's 0xA00-0xCFF out of scope). Others (Achievements.bin: no UI on the PS1; replays: out of
// scope) are not written: counted, reported saved, never found
struct PS1CardFile {
    const char *name;
    uint32 keep;
};
const PS1CardFile s_ps1CardFiles[] = { { "SaveData.bin", 0xA00 * sizeof(int32) }, { "Options.bin", 0 }, { "TimeAttackDB.bin", 0 } };

const PS1CardFile *PS1CardFileOf(const char *name)
{
    for (const PS1CardFile &f : s_ps1CardFiles)
        if (!strcmp(f.name, name))
            return &f;
    return NULL;
}

uint32 PS1CardWord(const uint8 *src, uint32 raw, uint32 w) // word w of src (bytes past raw read as 0)
{
    uint32 v = 0;
    uint32 o = w * 4, n = raw - o < 4 ? raw - o : 4;
    memcpy(&v, src + o, n);
    return v;
}

uint32 PS1CardPack(const uint8 *src, uint32 raw, uint32 *dst) // -> packed bytes (at most raw + 8); dst NULL: count only
{
    uint32 words = (raw + 3) / 4, n = 0;
    for (uint32 w = 0; w < words;) {
        uint32 z = w;
        while (z < words && !PS1CardWord(src, raw, z)) ++z;
        if (z > w) {
            if (dst)
                dst[n] = 0x80000000u | (z - w);
            ++n, w = z;
            continue;
        }
        uint32 lit = w;
        while (lit < words && (PS1CardWord(src, raw, lit) || (lit + 1 < words && PS1CardWord(src, raw, lit + 1)))) ++lit;
        if (dst)
            dst[n] = lit - w;
        ++n;
        for (; w < lit; ++w, ++n)
            if (dst)
                dst[n] = PS1CardWord(src, raw, w);
    }
    return n * 4;
}

bool PS1CardUnpack(const uint32 *src, uint32 packed, uint8 *dst, uint32 raw, uint32 cap) // cap: the bytes dst takes
{
    uint32 n = packed / 4, i = 0, o = 0, end = raw < cap ? raw : cap;
    while (i < n) {
        uint32 t = src[i++];
        uint32 count = t & 0x7FFFFFFF;
        for (uint32 k = 0; k < count; ++k, o += 4) {
            uint32 v = 0;
            if (!(t & 0x80000000u)) {
                if (i >= n)
                    return false;
                v = src[i++];
            }
            if (o < end)
                memcpy(dst + o, &v, end - o < 4 ? end - o : 4);
        }
    }
    return o >= raw;
}

// the staging block (DATASET_TMP's tail). TMP's freed blocks stay until a compaction (the time-attack table's 21 KB read
// buffer after its load): when the tail is short, TMP is compacted; the caller's buffer (*keep) may live in TMP (the
// table's write buffer): it is followed through its owner pointer, which the compaction updates
uint8 *PS1CardStage(uint32 size, void **keep)
{
    DataStorage *st = &dataStorage[DATASET_TMP];
    uint8 *p        = (uint8 *)PS1StorageTailAlloc(DATASET_TMP, size);
    g_ps1CardStageInfo[0] = size, g_ps1CardStageInfo[1] = st->usedStorage * 4, g_ps1CardStageInfo[2] = st->storageLimit;
    g_ps1CardStageInfo[3] = 0;
    if (!p) {
        bool inTmp   = *keep >= (void *)st->memoryTable && *keep < (void *)((uint8 *)st->memoryTable + st->storageLimit);
        void **owner = NULL;
        for (uint32 e = 0; inTmp && e < st->entryCount && !owner; ++e)
            if (st->storageEntries[e] == *keep && *st->dataEntries[e] == *keep)
                owner = (void **)st->dataEntries[e];
        g_ps1CardStageInfo[3] = (inTmp ? 1 : 0) | (owner ? 2 : 0);
        if (!inTmp || owner) {
            DefragmentAndGarbageCollectStorage(DATASET_TMP);
            g_ps1CardStageInfo[3] |= 4;
            g_ps1CardStageInfo[4] = st->usedStorage * 4;
            if (owner)
                *keep = *owner;
            p = (uint8 *)PS1StorageTailAlloc(DATASET_TMP, size);
        }
    }
    if (!p)
        g_ps1CardStageFail = g_ps1CardStageFail + 1;
    return p;
}

// the staged paths (a card file bigger than the resident copy): TMP stages it, a save reads it first, writes blocking
bool32 PS1CardLoadStaged(const char *filename, void *buffer, uint32 bufSize)
{
    uint32 room = PS1UserCardRoom();
    if (!room)
        return false; // no file (or no card)
    room       = room > PS1_CARD_ROOM ? PS1_CARD_ROOM : room;
    uint8 *buf = PS1CardStage(room, &buffer);
    if (!buf)
        return false;
    bool32 found = false;
    uint32 len   = 0;
    if (PS1UserCardRead(buf, room, &len)) {
        g_ps1CardErrors = g_ps1CardErrors + 1;
    }
    else if (len >= 8 && ((uint32 *)(buf + PS1_CARD_HEADER))[0] == PS1_CARD_MAGIC) {
        uint8 *p = buf + PS1_CARD_HEADER, *end = p + len;
        uint32 count = ((uint32 *)p)[1] & 0xFFFF;
        p += 8;
        for (uint32 i = 0; i < count && p + sizeof(PS1CardSection) <= end; ++i) {
            PS1CardSection *sec = (PS1CardSection *)p;
            p += sizeof(PS1CardSection);
            if (p + sec->packed > end)
                break;
            if (!strncmp(sec->name, filename, sizeof(sec->name))) {
                found = PS1CardUnpack((uint32 *)p, sec->packed, (uint8 *)buffer, sec->raw, bufSize);
                break;
            }
            p += sec->packed;
        }
        if (found)
            g_ps1CardLoads = g_ps1CardLoads + 1;
    }
    PS1StorageTailRelease(DATASET_TMP);
    return found;
}

bool32 PS1CardSaveStaged(const char *filename, void *buffer, uint32 raw)
{
    uint32 room = PS1UserCardRoom(); // the old file's (0: none)
    room        = room > PS1_CARD_ROOM ? PS1_CARD_ROOM : room;
    // the staging: the old file + this section packed (counted first: a table serializes to ~18 KB, mostly zeros)
    uint32 packed = PS1CardPack((const uint8 *)buffer, raw, NULL);
    uint32 cap    = (room > PS1_CARD_HEADER + 8 ? room : PS1_CARD_HEADER + 8) + sizeof(PS1CardSection) + packed;
    uint8 *buf  = PS1CardStage(cap, &buffer);
    if (!buf)
        return false;
    uint8 *c   = buf + PS1_CARD_HEADER;
    uint32 len = 0;
    if (!room || PS1UserCardRead(buf, room, &len) || len < 8 || ((uint32 *)c)[0] != PS1_CARD_MAGIC) {
        g_ps1CardFresh = g_ps1CardFresh + 1; // no file, or one this build can't read: a new one
        ((uint32 *)c)[0] = PS1_CARD_MAGIC;
        ((uint32 *)c)[1] = 0;
        len              = 8;
    }
    // drop this file's old section (the later ones move down)
    uint32 count = ((uint32 *)c)[1] & 0xFFFF, kept = 0;
    uint8 *p = c + 8, *end = c + len;
    for (uint32 i = 0; i < count && p + sizeof(PS1CardSection) <= end; ++i) {
        PS1CardSection *sec = (PS1CardSection *)p;
        uint32 bytes        = sizeof(PS1CardSection) + sec->packed;
        if (p + bytes > end)
            break;
        if (!strncmp(sec->name, filename, sizeof(sec->name))) {
            memmove(p, p + bytes, end - (p + bytes));
            end -= bytes;
            continue;
        }
        p += bytes, ++kept;
    }
    // this file's section at the end
    PS1CardSection *sec = (PS1CardSection *)p;
    memset(sec->name, 0, sizeof(sec->name));
    strncpy(sec->name, filename, sizeof(sec->name));
    sec->raw    = raw;
    sec->packed = PS1CardPack((const uint8 *)buffer, raw, (uint32 *)(p + sizeof(PS1CardSection)));
    p += sizeof(PS1CardSection) + sec->packed;
    ((uint32 *)c)[1] = kept + 1;
    uint32 total     = (uint32)(p - c);
    bool32 ok        = false;
    if (PS1_CARD_HEADER + total > PS1_CARD_ROOM) {
        g_ps1CardTooBig = g_ps1CardTooBig + 1; // the card keeps the last file
    }
    else if (PS1UserCardWrite(buf, total)) {
        g_ps1CardErrors = g_ps1CardErrors + 1;
    }
    else {
        g_ps1CardSaves   = g_ps1CardSaves + 1;
        g_ps1CardPayload = total;
        ok               = true;
    }
    PS1StorageTailRelease(DATASET_TMP);
    return ok;
}

// The resident copy of the card file (phase 8.4): a load or save reads the card only the first time (a short read: the
// file's first sectors), every save rewrites the file from it in the background (a write is 2.5 s, a read 2.4 s: a
// blocking read + write stood the game still ~5 s). A file bigger than the copy goes the staged way.
#define PS1_CARD_CACHE (1536) // the copy's container room (a typical file: ~200 B; a time-attack row packs to ~60 B)
uint32 s_ps1CardCache[(PS1_CARD_HEADER + PS1_CARD_CACHE) / 4]; // ps1/memcard's header room, then the container
uint32 s_ps1CardCacheLen   = 0;                               // its container bytes
bool s_ps1CardCacheValid   = false;                           // it holds the card's file (or the empty one: none there)
uint8 *PS1CardCacheC() { return (uint8 *)s_ps1CardCache + PS1_CARD_HEADER; }
void PS1CardCacheEmpty()
{
    ((uint32 *)PS1CardCacheC())[0] = PS1_CARD_MAGIC;
    ((uint32 *)PS1CardCacheC())[1] = 0;
    s_ps1CardCacheLen              = 8;
    s_ps1CardCacheValid            = true;
}
bool PS1CardCacheFill() // false: the card can't say (no card / unformatted) or its file is bigger than the copy
{
    if (s_ps1CardCacheValid)
        return true;
    if (!PS1UserCardRoom()) {
        if (g_ps1CardLastError != 10) // FileNotFound: a card without our file
            return false;
        PS1CardCacheEmpty();
        return true;
    }
    uint32 len = 0, e = PS1UserCardRead((uint8 *)s_ps1CardCache, sizeof(s_ps1CardCache), &len);
    if (e == 14) // SerializeOverflow: bigger than the copy
        return false;
    if (e || len < 8 || ((uint32 *)PS1CardCacheC())[0] != PS1_CARD_MAGIC) {
        if (e && e != 15) { // (BadData: a file this build can't read is replaced, as a missing one)
            g_ps1CardErrors = g_ps1CardErrors + 1;
            return false;
        }
        g_ps1CardFresh = g_ps1CardFresh + 1;
        PS1CardCacheEmpty();
        return true;
    }
    s_ps1CardCacheLen   = len;
    s_ps1CardCacheValid = true;
    return true;
}

bool32 PS1CardLoadUserFile(const char *filename, void *buffer, uint32 bufSize)
{
    if (!PS1CardFileOf(filename)) {
        g_ps1CardSkipped = g_ps1CardSkipped + 1;
        return false;
    }
    if (g_ps1CardOff)
        return false;
    if (!PS1CardCacheFill())
        return g_ps1CardLastError == 14 ? PS1CardLoadStaged(filename, buffer, bufSize) : false;
    uint8 *p = PS1CardCacheC(), *end = p + s_ps1CardCacheLen;
    uint32 count = ((uint32 *)p)[1] & 0xFFFF;
    p += 8;
    for (uint32 i = 0; i < count && p + sizeof(PS1CardSection) <= end; ++i) {
        PS1CardSection *sec = (PS1CardSection *)p;
        p += sizeof(PS1CardSection);
        if (p + sec->packed > end)
            break;
        if (!strncmp(sec->name, filename, sizeof(sec->name))) {
            bool32 found = PS1CardUnpack((uint32 *)p, sec->packed, (uint8 *)buffer, sec->raw, bufSize);
            if (found)
                g_ps1CardLoads = g_ps1CardLoads + 1;
            return found;
        }
        p += sec->packed;
    }
    return false;
}

bool32 PS1CardSaveUserFile(const char *filename, void *buffer, uint32 bufSize)
{
    const PS1CardFile *file = PS1CardFileOf(filename);
    if (!file) {
        g_ps1CardSkipped = g_ps1CardSkipped + 1;
        return true;
    }
    if (g_ps1CardOff)
        return false;
    uint32 raw = file->keep && file->keep < bufSize ? file->keep : bufSize;
    if (!PS1CardCacheFill())
        return PS1CardSaveStaged(filename, buffer, raw);
    PS1UserCardWait(); // the copy may still be on its way to the card
    // drop this file's old section (the later ones move down), then this one at the end, if the copy holds it
    uint8 *c = PS1CardCacheC(), *p = c + 8, *end = c + s_ps1CardCacheLen;
    uint32 count = ((uint32 *)c)[1] & 0xFFFF, kept = 0;
    for (uint32 i = 0; i < count && p + sizeof(PS1CardSection) <= end; ++i) {
        PS1CardSection *sec = (PS1CardSection *)p;
        uint32 bytes        = sizeof(PS1CardSection) + sec->packed;
        if (p + bytes > end)
            break;
        if (!strncmp(sec->name, filename, sizeof(sec->name))) {
            memmove(p, p + bytes, end - (p + bytes));
            end -= bytes;
            continue;
        }
        p += bytes, ++kept;
    }
    uint32 packed = PS1CardPack((const uint8 *)buffer, raw, NULL);
    if ((uint32)(p - c) + sizeof(PS1CardSection) + packed > PS1_CARD_CACHE) {
        s_ps1CardCacheValid = false; // too big for the copy from now on: the staged way (it reads the card's file)
        return PS1CardSaveStaged(filename, buffer, raw);
    }
    PS1CardSection *sec = (PS1CardSection *)p;
    memset(sec->name, 0, sizeof(sec->name));
    strncpy(sec->name, filename, sizeof(sec->name));
    sec->raw    = raw;
    sec->packed = PS1CardPack((const uint8 *)buffer, raw, (uint32 *)(p + sizeof(PS1CardSection)));
    p += sizeof(PS1CardSection) + sec->packed;
    ((uint32 *)c)[1]  = kept + 1;
    s_ps1CardCacheLen = (uint32)(p - c);
    if (PS1UserCardWriteAsync((uint8 *)s_ps1CardCache, s_ps1CardCacheLen)) {
        g_ps1CardErrors = g_ps1CardErrors + 1;
        return false;
    }
    g_ps1CardSaves   = g_ps1CardSaves + 1;
    g_ps1CardPayload = s_ps1CardCacheLen;
    return true;
}
} // namespace

// GDB test hook (tools/memcard/save_test.sh): at a frame start, g_ps1CardTest 1 = SaveUserFile, 2 = LoadUserFile of file
// (3 = SaveUserDB of table g_ps1CardTestFile)
// g_ps1CardTestFile (0 SaveData, 1 Options, 2 TimeAttackDB, 3 Achievements: skipped) with the buffer GDB points at
// (g_ps1CardTestPtr: the game's own, e.g. &globals->saveRAM) and size; the result in g_ps1CardTestResult
volatile uint32 g_ps1CardTest = 0, g_ps1CardTestFile = 0, g_ps1CardTestSize = 0, g_ps1CardTestResult = 0;
void *volatile g_ps1CardTestPtr = NULL;
void RSDK::SKU::PS1CardTestService()
{
    if (g_ps1CardTest == 3) { // SaveUserDB(table g_ps1CardTestFile): the UserDB path (TimeAttackDB)
        g_ps1CardTestResult = SaveUserDB((uint16)g_ps1CardTestFile, NULL);
        g_ps1CardTest       = 0;
        return;
    }
    if (!g_ps1CardTest || !g_ps1CardTestPtr)
        return;
    const char *name = g_ps1CardTestFile < 3 ? s_ps1CardFiles[g_ps1CardTestFile].name : "Achievements.bin";
    g_ps1CardTestResult = g_ps1CardTest == 1 ? SaveUserFile(name, g_ps1CardTestPtr, g_ps1CardTestSize)
                                              : LoadUserFile(name, g_ps1CardTestPtr, g_ps1CardTestSize);
    g_ps1CardTest = 0;
}
#endif

bool32 RSDK::SKU::LoadUserFile(const char *filename, void *buffer, uint32 bufSize)
{
#if RETRO_PLATFORM == RETRO_PS1
    return PS1CardLoadUserFile(filename, buffer, bufSize); // phase 8: the memory card
#endif
    if (preLoadSaveFileCB)
        preLoadSaveFileCB();

    char fullFilePath[0x400];
#if RETRO_USE_MOD_LOADER
    if (strlen(customUserFileDir))
        sprintf_s(fullFilePath, sizeof(fullFilePath), "%s%s", customUserFileDir, filename);
    else
        sprintf_s(fullFilePath, sizeof(fullFilePath), "%s%s", userFileDir, filename);
#else
    sprintf_s(fullFilePath, sizeof(fullFilePath), "%s%s", userFileDir, filename);
#endif
    PrintLog(PRINT_NORMAL, "Attempting to load user file: %s", fullFilePath);

    FileIO *file = fOpen(fullFilePath, "rb");
    if (file) {
        fSeek(file, 0, SEEK_END);
        uint32 fSize = (uint32)fTell(file);
        fSeek(file, 0, SEEK_SET);
        uint32 size = bufSize;
        if (bufSize > fSize)
            size = fSize;
        fRead(buffer, 1, size, file);
        fClose(file);

        if (postLoadSaveFileCB)
            postLoadSaveFileCB();

        return true;
    }
    else {
        if (postLoadSaveFileCB)
            postLoadSaveFileCB();

        PrintLog(PRINT_NORMAL, "Nope!");
    }

    return false;
}
bool32 RSDK::SKU::SaveUserFile(const char *filename, void *buffer, uint32 bufSize)
{
#if RETRO_PLATFORM == RETRO_PS1
    return PS1CardSaveUserFile(filename, buffer, bufSize); // phase 8: the memory card
#endif
    if (preLoadSaveFileCB)
        preLoadSaveFileCB();

    char fullFilePath[0x400];
#if RETRO_USE_MOD_LOADER
    if (strlen(customUserFileDir))
        sprintf_s(fullFilePath, sizeof(fullFilePath), "%s%s", customUserFileDir, filename);
    else
        sprintf_s(fullFilePath, sizeof(fullFilePath), "%s%s", userFileDir, filename);
#else
    sprintf_s(fullFilePath, sizeof(fullFilePath), "%s%s", userFileDir, filename);
#endif
    PrintLog(PRINT_NORMAL, "Attempting to save user file: %s", fullFilePath);

    FileIO *file = fOpen(fullFilePath, "wb");
    if (file) {
        fWrite(buffer, 1, bufSize, file);
        fClose(file);

        if (postLoadSaveFileCB)
            postLoadSaveFileCB();

        return true;
    }
    else {
        if (postLoadSaveFileCB)
            postLoadSaveFileCB();

        PrintLog(PRINT_NORMAL, "Nope!");
    }
    return false;
}
bool32 RSDK::SKU::DeleteUserFile(const char *filename)
{
    if (preLoadSaveFileCB)
        preLoadSaveFileCB();

    char fullFilePath[0x400];
#if RETRO_USE_MOD_LOADER
    if (strlen(customUserFileDir))
        sprintf_s(fullFilePath, sizeof(fullFilePath), "%s%s", customUserFileDir, filename);
    else
        sprintf_s(fullFilePath, sizeof(fullFilePath), "%s%s", userFileDir, filename);
#else
    sprintf_s(fullFilePath, sizeof(fullFilePath), "%s%s", userFileDir, filename);
#endif
    PrintLog(PRINT_NORMAL, "Attempting to delete user file: %s", fullFilePath);
    int32 status = remove(fullFilePath);

    if (postLoadSaveFileCB)
        postLoadSaveFileCB();

    return status == 0;
}

#if !RETRO_REV02
bool32 RSDK::SKU::TryLoadUserFile(const char *filename, void *buffer, uint32 size, void (*callback)(int32 status))
{
    bool32 success = false;

    success = LoadUserFile(filename, buffer, size);

    if (callback)
        callback(success ? STATUS_OK : STATUS_ERROR);

    return success;
}
bool32 RSDK::SKU::TrySaveUserFile(const char *filename, void *buffer, uint32 size, void (*callback)(int32 status))
{
    bool32 success = false;

    success = SaveUserFile(filename, buffer, size);

    if (callback)
        callback(success ? STATUS_OK : STATUS_ERROR);

    return success;
}
#endif

void RSDK::SKU::InitUserDirectory()
{
#if RETRO_PLATFORM == RETRO_OSX

    char buffer[0x100];
    sprintf_s(buffer, sizeof(buffer), "%s/RSDKv5/", getResourcesPath());
    SKU::SetUserFileCallbacks(buffer, NULL, NULL);

#elif RETRO_PLATFORM == RETRO_ANDROID

    // done by javaside

#elif RETRO_PLATFORM == RETRO_LINUX

    SKU::SetUserFileCallbacks("./", NULL, NULL);

#else

    SKU::SetUserFileCallbacks("", NULL, NULL);

#endif
}