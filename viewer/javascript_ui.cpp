#include "javascript_ui.hpp"

#include "jsonxx_utils.hpp"

#include <parser/klass_register.hpp>

#include <limits>
#include <algorithm>

namespace HawkTracer
{
namespace viewer
{

using namespace parser;


JavaScriptUI::JavaScriptUI(int port)
{
    // TODO error handling
    _server_th = std::thread([this, port] {
        _server.start(
                    port,
                    [this] { _client_connected(); },
        [this] (const std::string& msg) { _client_message_received(msg); });
    });
}

JavaScriptUI::~JavaScriptUI()
{
    _server.stop();
    _server_th.join();
}

void JavaScriptUI::_client_connected()
{
    _request_klass_register();
    _send_graphs_info();
}

void JavaScriptUI::_client_message_received(const std::string& message)
{
    jsonxx::Object obj;

    if (!obj.parse(message))
    {
        std::cerr << "Unable to parse message: " << std::endl << message << std::endl;
    }

    auto command = obj.get<jsonxx::String>("command", "");
    if (command == "createGraph")
    {
        if (!check_required_fields<jsonxx::String, jsonxx::String, jsonxx::Number, jsonxx::Object>(obj, {"graphTypeId", "graphId", "klassId", "fieldMap"}))
        {
            _send_missing_fields_error(message);
        }
        else
        {
            HT_EventKlassId klass_id = obj.get<jsonxx::Number>("klassId");
            Graph::TypeId graph_type = obj.get<jsonxx::String>("graphTypeId");
            Graph::Id graph_id = obj.get<jsonxx::String>("graphId");
            jsonxx::Object field_map = obj.get<jsonxx::Object>("fieldMap");

            if (_graphs.find(graph_id) != _graphs.end())
            {
                _send_request_error("Graph '" + graph_id + "' alread exists", message);
            }

            Graph::FieldMapping mapping;
            for (const auto& value : field_map.kv_map())
            {
                mapping[value.first] = value.second->get<jsonxx::String>();
            }

            _graphs[graph_id] = _graph_factory.create_graph(graph_type, klass_id, graph_id, std::move(mapping));
        }
    }
    else if (command == "setCanvasSize")
    {
        if (!check_required_fields<jsonxx::Number>(obj, {"value"}))
        {
            _send_missing_fields_error(message);
        }
        else
        {
            _canvas_size = obj.get<jsonxx::Number>("value");
        }
    }
    else if (command == "getTotalTSRange")
    {
        _send_json_object(make_json_object(
                              "command", "totalTSRange",
                              "startTimestamp", _get_total_ts_range().start,
                              "stopTimestamp", _get_total_ts_range().stop));
    }
    else if (command == "setCurrentTSRange")
    {
        if (!check_required_fields<jsonxx::Number, jsonxx::Number>(obj, {"duration", "stopTimestamp"}))
        {
            _send_missing_fields_error(message);
        }
        else
        {
            auto duration = static_cast<HT_DurationNs>(obj.get<jsonxx::Number>("duration", 0));
            auto stop_ts = static_cast<HT_TimestampNs>(obj.get<jsonxx::Number>("stopTimestamp", 0));
            _set_time_range(duration, stop_ts);
        }
    }
    else
    {
        for (const auto& graph_p : _graphs)
        {
            const auto& graph = graph_p.second;
            jsonxx::Object data = graph->create_graph_data(
                        _request_data(graph->get_klass_id()),
                        _get_current_ts_range(),
                        _canvas_size);

            auto obj = make_json_object("command", "data", "graph_id", graph->get_id());
            append_to_json_object(obj, "data", data);
            _send_json_object(obj);
        }
    }
}

void JavaScriptUI::add_klass(const parser::EventKlass* klass)
{
    _send_json_object(make_json_object(
                          "command", "addKlass", "name", klass->get_name(),
                          "id", klass->get_id()));
}

void JavaScriptUI::add_field(const parser::EventKlass* klass, const parser::EventKlassField* field)
{
    if (field->get_type_id() == FieldTypeId::STRUCT)
    {
        auto klass_id = KlassRegister::get().get_klass_id(field->get_type_name());
        for (const auto& klass_field : KlassRegister::get().get_klass(klass_id)->get_fields())
        {
            add_field(klass, klass_field.get());
        }
    }
    else
    {
        _send_json_object(make_json_object(
                              "command", "addField",
                              "name", field->get_name(), "klassId", klass->get_id()));
    }
}

void JavaScriptUI::_send_graphs_info()
{
    jsonxx::Array graphs;
    for (const auto& graph_info : _graph_factory.get_graphs_info())
    {
        jsonxx::Array fields;
        for (const auto& field : graph_info.second.fields)
        {
            fields << field;
        }

        graphs << make_json_object("id", graph_info.second.type_id,
                                   "fields", fields);
    }
    _send_json_object(make_json_object("command", "graphTypes", "graphs", graphs));
}

void JavaScriptUI::_send_json_object(const jsonxx::Object& obj)
{
    _server.send_text(obj.json());
}

void JavaScriptUI::_send_request_error(const std::string& message, const std::string& raw_json)
{
    _send_error("Error:\n" + message + "\nRaw json: " + raw_json);
}

void JavaScriptUI::_send_error(const std::string& message)
{
    _send_json_object(make_json_object("command", "error", "message", message));
}

void JavaScriptUI::_send_missing_fields_error(const std::string& raw_json)
{
    _send_request_error("missing required fields in a request", raw_json);
}

} // namespace viewer
} // namespace HawkTracer
