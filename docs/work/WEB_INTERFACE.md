# Web-Based System Interface

**Status**: Concept Phase  
**Priority**: Low  
**Created**: February 12, 2026

## Overview

Generic web-based monitoring and control interface for CommRaT systems. Provides REST API with zero boilerplate and real-time data streaming via WebSockets.

## Motivation

Real-time systems need monitoring and control interfaces, but building custom GUIs is time-consuming. A generic web interface that automatically adapts to the system's message types and modules eliminates this overhead.

## Architecture

```
┌─────────────────┐
│   Web Browser   │
│                 │
│  ┌───────────┐  │
│  │  React    │  │  WebSocket: Real-time data streaming
│  │  Dashboard│  │◄─────────────────────────┐
│  └───────────┘  │                          │
│       │         │                          │
│       │ HTTP    │                          │
│       ↓         │                          │
└───────┼─────────┘                          │
        │                                    │
        │ REST API                           │
        ↓                                    │
┌──────────────────────────────────────┐    │
│     CommRaT WebInterface Module      │    │
│                                      │    │
│  ┌────────────┐    ┌──────────────┐ │    │
│  │ REST       │    │  WebSocket   │◄┼────┘
│  │ Server     │    │  Server      │ │
│  └────────────┘    └──────────────┘ │
│         │                  │         │
│         └──────────┬───────┘         │
│                    │                 │
│         ┌──────────▼──────────┐      │
│         │  CommRaT Subscriber │      │
│         │  (All message types)│      │
│         └─────────────────────┘      │
└──────────────────────────────────────┘
                │
                │ TiMS Messages
                ↓
        ┌───────────────┐
        │  CommRaT      │
        │  System       │
        │  (All modules)│
        └───────────────┘
```

## Features

### 1. Automatic API Generation

**Zero Configuration**: REST API automatically generated from message registry

```cpp
// Application definition
using MyApp = CommRaT<
    Message::Data<TemperatureData>,
    Message::Data<PressureData>,
    Message::Command<ResetCmd>
>;

// WebInterface module
class WebInterface : public MyApp::Module<
    NoOutput,  // Doesn't produce data, only monitors
    Inputs<TemperatureData, PressureData>  // Subscribe to all data
> {
    // Automatically generates:
    // GET  /api/messages/TemperatureData/latest
    // GET  /api/messages/TemperatureData/history?limit=100
    // GET  /api/messages/PressureData/latest
    // POST /api/commands/ResetCmd
    // GET  /api/modules/list
    // GET  /api/modules/{id}/status
    // WebSocket: ws://localhost:8080/stream
};
```

### 2. REST API Endpoints

**Message Access**:
```
GET  /api/messages/{type}/latest
GET  /api/messages/{type}/history?limit={n}&since={timestamp}
GET  /api/messages/types
```

**Module Control**:
```
GET  /api/modules/list
GET  /api/modules/{system_id}/{instance_id}/status
POST /api/modules/{system_id}/{instance_id}/lifecycle/on
POST /api/modules/{system_id}/{instance_id}/lifecycle/off
POST /api/modules/{system_id}/{instance_id}/lifecycle/reset
```

**Parameters**:
```
GET  /api/modules/{system_id}/{instance_id}/params
GET  /api/modules/{system_id}/{instance_id}/params/{name}
PUT  /api/modules/{system_id}/{instance_id}/params/{name}
POST /api/modules/{system_id}/{instance_id}/params/save
```

**Commands**:
```
POST /api/commands/{type}
GET  /api/commands/types
```

**System Information**:
```
GET  /api/system/info
GET  /api/system/topology  # Module graph with connections
```

### 3. WebSocket Streaming

**Real-Time Data Push**:
```javascript
const ws = new WebSocket('ws://localhost:8080/stream');

// Subscribe to specific message types
ws.send(JSON.stringify({
    action: 'subscribe',
    types: ['TemperatureData', 'PressureData']
}));

// Receive real-time updates
ws.onmessage = (event) => {
    const msg = JSON.parse(event.data);
    console.log(msg.type, msg.data, msg.timestamp);
};
```

**Filtering**:
```javascript
// Subscribe with filters
ws.send(JSON.stringify({
    action: 'subscribe',
    types: ['TemperatureData'],
    filter: {
        system_id: 10,
        instance_id: 1,
        rate_limit_hz: 10  // Downsample to 10 Hz
    }
}));
```

### 4. React Dashboard

**Pre-Built Components**:
- Real-time line charts
- Gauge displays
- Module status cards
- Command buttons
- Parameter editors
- Log viewer
- System topology graph

**Example Usage**:
```tsx
import { CommRatDashboard, MessageChart, ModuleCard } from 'commrat-webgui';

function MyDashboard() {
    return (
        <CommRatDashboard apiUrl="http://localhost:8080">
            <MessageChart 
                messageType="TemperatureData"
                fields={['temperature_c', 'confidence']}
                timeWindow={60}  // 60 seconds
            />
            
            <ModuleCard 
                systemId={10}
                instanceId={1}
                showParams={true}
                showCommands={true}
            />
        </CommRatDashboard>
    );
}
```

## Implementation

### WebInterface Module

```cpp
template<typename AppType>
class WebInterface : public AppType::template Module<
    NoOutput,
    NoInput  // Or subscribe to specific types
> {
    using Base = typename AppType::template Module<NoOutput, NoInput>;
    
    // HTTP server (Beast or similar)
    HttpServer http_server_;
    
    // WebSocket server
    WebSocketServer ws_server_;
    
    // Message history (ring buffers per type)
    std::unordered_map<uint32_t, MessageHistory> history_;
    
public:
    struct WebInterfaceConfig {
        std::string bind_address{"0.0.0.0"};
        uint16_t http_port{8080};
        uint16_t ws_port{8081};
        size_t history_size{1000};  // Per message type
        bool enable_cors{true};
    };
    
    explicit WebInterface(const ModuleConfig& config, 
                         const WebInterfaceConfig& web_config)
        : Base(config)
        , http_server_(web_config.bind_address, web_config.http_port)
        , ws_server_(web_config.bind_address, web_config.ws_port)
    {
        setup_routes();
        start_servers();
    }
    
private:
    void setup_routes() {
        // Auto-generate routes from registry
        AppType::for_each_message_type([this](auto type_info) {
            const char* type_name = type_info.name;
            uint32_t type_id = type_info.id;
            
            // GET /api/messages/{type}/latest
            http_server_.add_route(
                "GET", 
                fmt::format("/api/messages/{}/latest", type_name),
                [this, type_id](const Request& req) {
                    return get_latest_message(type_id);
                }
            );
            
            // GET /api/messages/{type}/history
            http_server_.add_route(
                "GET",
                fmt::format("/api/messages/{}/history", type_name),
                [this, type_id](const Request& req) {
                    size_t limit = req.query_param<size_t>("limit", 100);
                    return get_message_history(type_id, limit);
                }
            );
        });
        
        // Module control routes
        http_server_.add_route("GET", "/api/modules/list", 
                              [this](auto& req) { return list_modules(); });
        
        // Command routes
        http_server_.add_route("POST", "/api/commands/:type",
                              [this](auto& req) { return send_command(req); });
    }
    
    Response get_latest_message(uint32_t type_id) {
        auto it = history_.find(type_id);
        if (it == history_.end() || it->second.empty()) {
            return Response{404, "No messages of this type"};
        }
        
        // Serialize latest message to JSON
        const auto& latest = it->second.latest();
        std::string json = AppType::Introspection::export_as<JSON>(latest);
        
        return Response{200, json, "application/json"};
    }
    
    void on_message_received(uint32_t type_id, const std::byte* data, size_t size) {
        // Store in history
        history_[type_id].push(data, size);
        
        // Push to WebSocket clients
        ws_server_.broadcast(type_id, data, size);
    }
};
```

### Separate Repository

**commrat-webgui**: Separate repository with:
- C++ WebInterface module (header-only or compiled)
- React component library
- Example dashboards
- Docker deployment

## Benefits

1. **Zero Boilerplate**: API automatically generated from message registry
2. **Real-Time**: WebSocket streaming provides live updates
3. **Type-Safe**: All API endpoints validated against registry
4. **Customizable**: Pre-built components or custom React code
5. **Portable**: Web interface accessible from any device
6. **Development Tool**: Essential for debugging and system monitoring

## Use Cases

### Development
- Monitor message flow in real-time
- Send test commands
- Adjust parameters without recompiling
- View module health and status

### Deployment
- Production system monitoring
- Remote control interface
- Data visualization
- System diagnostics

### Demos
- Live system demonstrations
- Customer presentations
- Training and education

## Implementation Plan

**Phase 1**: REST API server (3 weeks)
- HTTP server integration (Boost.Beast or similar)
- Auto-generate routes from registry
- JSON serialization via introspection

**Phase 2**: WebSocket streaming (2 weeks)
- WebSocket server
- Message subscription and filtering
- Rate limiting and downsampling

**Phase 3**: React dashboard (4 weeks)
- Component library
- Real-time charts and visualizations
- Module control interface

**Phase 4**: Documentation and examples (1 week)

**Total Estimated Effort**: 10 weeks (separate from CommRaT core)

## Dependencies

- CommRaT introspection system (JSON export)
- HTTP library (Boost.Beast, cpp-httplib, etc.)
- WebSocket library (websocketpp, Beast)
- JSON library (already have via rfl)
- React (separate frontend)

## Related Work

- Introspection: `docs/work/INTROSPECTION_INTEGRATION_PLAN.md`
- Parameter system: `docs/work/PARAMETER_SYSTEM.md`
- Lifecycle system: `docs/work/LIFECYCLE_SYSTEM.md`
- Module types: `docs/work/MODULE_TYPE_SYSTEM.md`
