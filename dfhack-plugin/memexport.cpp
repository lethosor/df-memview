#include "Console.h"
#include "Core.h"
#include "DataDefs.h"
#include "Export.h"
#include "MemAccess.h"
#include "PluginManager.h"

#include "ActiveSocket.h"
#include "PassiveSocket.h"
#include "md5wrapper.h"

//#include "df/world.h"

using namespace DFHack;

DFHACK_PLUGIN("memexport");
DFHACK_PLUGIN_IS_ENABLED(is_enabled);

static CPassiveSocket server;

class ClientWrapper {
    bool error;
    CActiveSocket *client;
public:
    ClientWrapper(CActiveSocket* client) : client(client), error(false) {}

    template <class T>
    bool Send (T value)
    {
        if (!error && !rawSend(value))
            error = true;
        return !error;
    }

    template <class T>
    bool Send (T value, size_t length)
    {
        if (!error && !rawSend(value, length))
            error = true;
        return !error;
    }

    inline bool Success() { return !error; }
    inline bool Fail() { error = true; }

protected:
    bool rawSend(std::string s)
    {
        return client->Send((const uint8_t*)s.c_str(), s.size()) == s.size();
    }
    bool rawSend(const uint8_t* data, size_t length)
    {
        return client->Send(data, length) == length;
    }

    #define DEFINE_RAW_SEND(int_type) \
        bool rawSend(int_type x) \
            { return sizeof(int_type) == client->Send((const uint8_t*)&x, sizeof(int_type)); }

    DEFINE_RAW_SEND(uint8_t);
    DEFINE_RAW_SEND(uint32_t);
    DEFINE_RAW_SEND(uint64_t);
    DEFINE_RAW_SEND(uintptr_t);

    #undef DEFINE_RAW_SEND
};

DFhackCExport command_result plugin_enable (color_ostream &out, bool state);

DFhackCExport command_result plugin_init (color_ostream &out, std::vector <PluginCommand> &commands)
{
    return CR_OK;
}

DFhackCExport command_result plugin_shutdown (color_ostream &out)
{
    return plugin_enable(out, false);
}

DFhackCExport command_result plugin_enable (color_ostream &out, bool state)
{
    if (state != is_enabled)
    {
        if (state)
        {
            server.Initialize();
            server.SetOptionReuseAddr();
            if (!server.Listen((const uint8_t*)"127.0.0.1", 5020))
                return CR_FAILURE;
            server.SetNonblocking();
        }
        else
        {
            server.Close();
        }
        is_enabled = state;
    }
    return CR_OK;
}

/*
Format:

uint32_t process_id
uint8_t arch            // 32 or 64
uint32_t num_ranges
struct {
    uint32_t name_length
    char name[name_length]
    uint8_t can_read
    uint8_t can_write
    uint8_t can_execute
    uint8_t is_shared
    uint64_t buffer_start;
    uint32_t buffer_length
    uint8_t buffer[buffer_length]
    uint8_t name_md5[32]
}[num_ranges]

*/

DFhackCExport command_result plugin_onupdate (color_ostream &out)
{
    static const int chunk_size = 4096;
    static uint8_t buffer[chunk_size];
    static md5wrapper md5;
    if (is_enabled)
    {
        CActiveSocket *client = server.Accept();
        if (client) {
            CoreSuspender suspend;
            client->SetBlocking();
            out.print("Exporting memory contents\n");
            ClientWrapper wrapper(client);
            Process *proc = Core::getInstance().p;
            wrapper.Send(uint32_t(proc->getPID()));
            wrapper.Send(uint8_t(sizeof(void*) * 8));

            std::vector<t_memrange> orig_ranges, ranges;
            proc->getMemRanges(orig_ranges);
            for (auto r = orig_ranges.begin(); r != orig_ranges.end(); ++r)
            {
                if (!r->valid || !r->read)
                    continue;
                ranges.push_back(*r);
            }
            wrapper.Send(uint32_t(ranges.size()));
            size_t index = 0;
            for (auto r = ranges.begin(); wrapper.Success() && r != ranges.end(); ++r)
            {
                std::string name(r->name);
                out.print("  [%3i/%3i] Exporting range: %s at %p\n", ++index, ranges.size(), name.c_str(), r->start);
                wrapper.Send(uint32_t(name.size()));
                wrapper.Send(name);
                wrapper.Send(uint8_t(r->read));
                wrapper.Send(uint8_t(r->write));
                wrapper.Send(uint8_t(r->execute));
                wrapper.Send(uint8_t(r->shared));

                wrapper.Send(uintptr_t(r->start));
                wrapper.Send(uint32_t((uint8_t*)r->end - (uint8_t*)r->start));

                auto *pos = (uint8_t*)r->start;
                auto *end = (uint8_t*)r->end;
                int sent;
                size_t cur_size;
                while (pos < end)
                {
                    cur_size = std::min(end - pos, chunk_size);
                    memcpy(buffer, pos, cur_size);
                    sent = client->Send(buffer, cur_size);
                    if (sent <= 0)
                    {
                        out.printerr("%s\n", client->DescribeError());
                        wrapper.Fail();
                        break;
                    }
                    pos += sent;
                }
                wrapper.Send(md5.getHashFromString(name));
            }

            if (wrapper.Success())
                out.print("Memory export complete\n");
            else
                out.printerr("Memory export failed!\n");
            client->Close();
            delete client;
        }
    }
}
