#ifndef Server_Side_RPC_h
#define Server_Side_RPC_h

// Local includes.
#include "RPC_Callback.h"
#include "IAPI_Implementation.h"


// Server side RPC topics.
char constexpr RPC_SUBSCRIBE_TOPIC[] = "v1/devices/me/rpc/request/+";
char constexpr RPC_REQUEST_TOPIC[] = "v1/devices/me/rpc/request/";
char constexpr RPC_SEND_RESPONSE_TOPIC[] = "v1/devices/me/rpc/response/%u";
// Log messages.
char constexpr RPC_RESPONSE_OVERFLOWED[] = "Server-side RPC response overflowed, increase MaxRPC (%u)";
#if !THINGSBOARD_ENABLE_DYNAMIC
char constexpr SERVER_SIDE_RPC_SUBSCRIPTIONS[] = "server-side RPC";
#endif // !THINGSBOARD_ENABLE_DYNAMIC
#if THINGSBOARD_ENABLE_DEBUG
char constexpr SERVER_RPC_METHOD_NULL[] = "Server-side RPC method name is NULL";
char constexpr RPC_RESPONSE_NULL[] = "Response JsonDocument is NULL, skipping sending";
char constexpr NO_RPC_PARAMS_PASSED[] = "No parameters passed with RPC, passing null JSON";
char constexpr CALLING_RPC_CB[] = "Calling subscribed callback for rpc with methodname (%s)";
#endif // THINGSBOARD_ENABLE_DEBUG


/// @brief Handles the internal implementation of the ThingsBoard server side RPC API.
/// See https://thingsboard.io/docs/user-guide/rpc/#server-side-rpc for more information
/// @tparam Logger Implementation that should be used to print error messages generated by internal processes and additional debugging messages if THINGSBOARD_ENABLE_DEBUG is set, default = DefaultLogger
#if THINGSBOARD_ENABLE_DYNAMIC
template <typename Logger = DefaultLogger>
#else
/// @tparam MaxSubscriptions Maximum amount of simultaneous server side rpc subscriptions.
/// Once the maximum amount has been reached it is not possible to increase the size, this is done because it allows to allcoate the memory on the stack instead of the heap, default = Default_Subscriptions_Amount (1)
/// @tparam MaxRPC Maximum amount of key-value pairs that will ever be sent in the subscribed callback method of an RPC_Callback, allows to use a StaticJsonDocument on the stack in the background.
/// If we simply use .to<JsonVariant>(); on the received document and use .set() to change the internal value then the size requirements are 0.
/// However if we attempt to send multiple key-value pairs, we have to adjust the size accordingly. See https://arduinojson.org/v6/assistant/ for more information on how to estimate the required size and divide the result by 16 to receive the required MaxRPC value, default = Default_RPC_Amount (0)
template<size_t MaxSubscriptions = Default_Subscriptions_Amount, size_t MaxRPC = Default_RPC_Amount, typename Logger = DefaultLogger>
#endif // THINGSBOARD_ENABLE_DYNAMIC
class Server_Side_RPC : public IAPI_Implementation {
  public:
    /// @brief Constructor
    Server_Side_RPC() = default;

    /// @brief Subscribes multiple server side RPC callbacks,
    /// that will be called if a request from the server for the method with the given name is received.
    /// Can be called even if we are currently not connected to the cloud,
    /// this is the case because the only interaction that requires an active connection is the subscription of the topic that we receive the response on
    /// and that subscription is also done automatically by the library once the device has established a connection to the cloud.
    /// Therefore this method can simply be called once at startup before a connection has been established
    /// and will then automatically handle the subscription of the topic once the connection has been established.
    /// See https://thingsboard.io/docs/user-guide/rpc/#server-side-rpc for more information
    /// @tparam InputIterator Class that points to the begin and end iterator
    /// of the given data container, allows for using / passing either std::vector or std::array.
    /// See https://en.cppreference.com/w/cpp/iterator/input_iterator for more information on the requirements of the iterator
    /// @param first Iterator pointing to the first element in the data container
    /// @param last Iterator pointing to the end of the data container (last element + 1)
    /// @return Whether subscribing the given callbacks was successful or not
    template<typename InputIterator>
    bool RPC_Subscribe(InputIterator const & first, InputIterator const & last) {
#if !THINGSBOARD_ENABLE_DYNAMIC
        size_t const size = Helper::distance(first, last);
        if (m_rpc_callbacks.size() + size > m_rpc_callbacks.capacity()) {
            Logger::printfln(MAX_SUBSCRIPTIONS_EXCEEDED, MAX_SUBSCRIPTIONS_TEMPLATE_NAME, SERVER_SIDE_RPC_SUBSCRIPTIONS);
            return false;
        }
#endif // !THINGSBOARD_ENABLE_DYNAMIC
        (void)m_subscribe_topic_callback.Call_Callback(RPC_SUBSCRIBE_TOPIC);
        // Push back complete vector into our local m_rpc_callbacks vector.
        m_rpc_callbacks.insert(m_rpc_callbacks.end(), first, last);
        return true;
    }

    /// @brief Subscribe one server side RPC callback,
    /// that will be called if a request from the server for the method with the given name is received.
    /// Can be called even if we are currently not connected to the cloud,
    /// this is the case because the only interaction that requires an active connection is the subscription of the topic that we receive the response on
    /// and that subscription is also done automatically by the library once the device has established a connection to the cloud.
    /// Therefore this method can simply be called once at startup before a connection has been established
    /// and will then automatically handle the subscription of the topic once the connection has been established.
    /// See https://thingsboard.io/docs/user-guide/rpc/#server-side-rpc for more information
    /// @param callback Callback method that will be called
    /// @return Whether subscribing the given callback was successful or not
    bool RPC_Subscribe(RPC_Callback const & callback) {
#if !THINGSBOARD_ENABLE_DYNAMIC
        if (m_rpc_callbacks.size() + 1 > m_rpc_callbacks.capacity()) {
            Logger::printfln(MAX_SUBSCRIPTIONS_EXCEEDED, MAX_SUBSCRIPTIONS_TEMPLATE_NAME, SERVER_SIDE_RPC_SUBSCRIPTIONS);
            return false;
        }
#endif // !THINGSBOARD_ENABLE_DYNAMIC
        (void)m_subscribe_topic_callback.Call_Callback(RPC_SUBSCRIBE_TOPIC);
        m_rpc_callbacks.push_back(callback);
        return true;
    }

    /// @brief Unsubcribes all server side RPC callbacks.
    /// See https://thingsboard.io/docs/user-guide/rpc/#server-side-rpc for more information
    /// @return Whether unsubcribing all the previously subscribed callbacks
    /// and from the rpc topic, was successful or not
    bool RPC_Unsubscribe() {
        m_rpc_callbacks.clear();
        return m_unsubscribe_topic_callback.Call_Callback(RPC_SUBSCRIBE_TOPIC);
    }

    API_Process_Type Get_Process_Type() const override {
        return API_Process_Type::JSON;
    }

    void Process_Response(char const * topic, uint8_t * payload, unsigned int length) override {
        // Nothing to do
    }

    void Process_Json_Response(char const * topic, JsonDocument const & data) override {
        if (!data.containsKey(RPC_METHOD_KEY)) {
#if THINGSBOARD_ENABLE_DEBUG
            Logger::println(SERVER_RPC_METHOD_NULL);
#endif // THINGSBOARD_ENABLE_DEBUG
            return;
        }
        char const * method_name = data[RPC_METHOD_KEY];

#if THINGSBOARD_ENABLE_STL
        auto it = std::find_if(m_rpc_callbacks.begin(), m_rpc_callbacks.end(), [&method_name](RPC_Callback const & rpc) {
            char const * subscribedMethodName = rpc.Get_Name();
            return (!Helper::stringIsNullorEmpty(subscribedMethodName) && strncmp(subscribedMethodName, method_name, strlen(subscribedMethodName)) == 0);
        });
        if (it != m_rpc_callbacks.end()) {
            auto & rpc = *it;
#else
        for (auto const & rpc : m_rpc_callbacks) {
            char const * subscribedMethodName = rpc.Get_Name();
            if (Helper::stringIsNullorEmpty(subscribedMethodName) || strncmp(subscribedMethodName, method_name, strlen(subscribedMethodName)) != 0) {
              continue;
            }
#endif // THINGSBOARD_ENABLE_STL
#if THINGSBOARD_ENABLE_DEBUG
            if (!data.containsKey(RPC_PARAMS_KEY)) {
                Logger::println(NO_RPC_PARAMS_PASSED);
            }
#endif // THINGSBOARD_ENABLE_DEBUG

#if THINGSBOARD_ENABLE_DEBUG
            Logger::printfln(CALLING_RPC_CB, method_name);
#endif // THINGSBOARD_ENABLE_DEBUG

            JsonVariantConst const param = data[RPC_PARAMS_KEY];
#if THINGSBOARD_ENABLE_DYNAMIC
            size_t const & rpc_response_size = rpc.Get_Response_Size();
            TBJsonDocument json_buffer(rpc_response_size);
#else
            size_t constexpr rpc_response_size = MaxRPC;
            StaticJsonDocument<JSON_OBJECT_SIZE(MaxRPC)> json_buffer;
#endif // THINGSBOARD_ENABLE_DYNAMIC
            rpc.Call_Callback(param, json_buffer);

            if (json_buffer.isNull()) {
#if THINGSBOARD_ENABLE_DEBUG
                Logger::println(RPC_RESPONSE_NULL);
#endif // THINGSBOARD_ENABLE_DEBUG
                return;
            }
            else if (json_buffer.overflowed()) {
                Logger::printfln(RPC_RESPONSE_OVERFLOWED, rpc_response_size);
                return;
            }

            size_t const request_id = Helper::parseRequestId(RPC_REQUEST_TOPIC, topic);
            char responseTopic[Helper::detectSize(RPC_SEND_RESPONSE_TOPIC, request_id)] = {};
            (void)snprintf(responseTopic, sizeof(responseTopic), RPC_SEND_RESPONSE_TOPIC, request_id);
            (void)m_send_json_callback.Call_Callback(responseTopic, json_buffer, Helper::Measure_Json(json_buffer));
            return;
        }
    }

    bool Compare_Response_Topic(char const * topic) const override {
        return strncmp(RPC_REQUEST_TOPIC, topic, strlen(RPC_REQUEST_TOPIC)) == 0;
    }

    bool Unsubscribe() override {
        return RPC_Unsubscribe();
    }

    bool Resubscribe_Topic() override {
        if (!m_rpc_callbacks.empty() && !m_subscribe_topic_callback.Call_Callback(RPC_SUBSCRIBE_TOPIC)) {
            Logger::printfln(SUBSCRIBE_TOPIC_FAILED, RPC_SUBSCRIBE_TOPIC);
            return false;
        }
        return true;
    }

#if !THINGSBOARD_USE_ESP_TIMER
    void loop() override {
        // Nothing to do
    }
#endif // !THINGSBOARD_USE_ESP_TIMER

    void Initialize() override {
        // Nothing to do
    }

    void Set_Client_Callbacks(Callback<void, IAPI_Implementation &>::function subscribe_api_callback, Callback<bool, char const * const, JsonDocument const &, size_t const &>::function send_json_callback, Callback<bool, char const * const, char const * const>::function send_json_string_callback, Callback<bool, char const * const>::function subscribe_topic_callback, Callback<bool, char const * const>::function unsubscribe_topic_callback, Callback<uint16_t>::function get_size_callback, Callback<bool, uint16_t>::function set_buffer_size_callback, Callback<size_t *>::function get_request_id_callback) override {
        m_send_json_callback.Set_Callback(send_json_callback);
        m_subscribe_topic_callback.Set_Callback(subscribe_topic_callback);
        m_unsubscribe_topic_callback.Set_Callback(unsubscribe_topic_callback);
    }

  private:
    Callback<bool, char const * const, JsonDocument const &, size_t const &> m_send_json_callback = {};         // Send json document callback
    Callback<bool, char const * const>                                       m_subscribe_topic_callback = {};   // Subscribe mqtt topic client callback
    Callback<bool, char const * const>                                       m_unsubscribe_topic_callback = {}; // Unubscribe mqtt topic client callback

    // Vectors or array (depends on wheter if THINGSBOARD_ENABLE_DYNAMIC is set to 1 or 0), hold copy of the actual passed data, this is to ensure they stay valid,
    // even if the user only temporarily created the object before the method was called.
    // This can be done because all Callback methods mostly consists of pointers to actual object so copying them
    // does not require a huge memory overhead and is acceptable especially in comparsion to possible problems that could
    // arise if references were used and the end user does not take care to ensure the Callbacks live on for the entirety
    // of its usage, which will lead to dangling references and undefined behaviour.
    // Therefore copy-by-value has been choosen as for this specific use case it is more advantageous,
    // especially because at most we copy internal vectors or array, that will only ever contain a few pointers
#if THINGSBOARD_ENABLE_DYNAMIC
    Vector<RPC_Callback>                                                     m_rpc_callbacks = {};              // Server side RPC callbacks vector
#else
    Array<RPC_Callback, MaxSubscriptions>                                    m_rpc_callbacks = {};              // Server side RPC callbacks array
#endif // THINGSBOARD_ENABLE_DYNAMIC
};

#endif // Server_Side_RPC_h