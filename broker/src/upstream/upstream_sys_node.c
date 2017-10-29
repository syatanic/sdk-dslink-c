#define LOG_TAG "upstream"
#include <dslink/log.h>

#include <dslink/utils.h>

#include <broker/upstream/upstream_node.h>
#include <broker/utils.h>
#include <broker/upstream/upstream_handshake.h>
#include <broker/broker.h>
#include <broker/config.h>

void add_upstream_invoke(RemoteDSLink *link,
                         BrokerNode *node,
                         json_t *req, PermissionLevel maxPermission);
BrokerNode* add_new_upstream(BrokerNode *parentNode,
                             json_t* namejson,
                             json_t* brokerNameJson,
                             json_t* urlJson,
                             json_t* tokenJson,
                             json_t* groupJson,
                             json_t* enabledJson);
void delete_upstream_file(const char* name);

static
void save_upstream_node(BrokerNode *node) {
    char tmp[128];
    const char *base = broker_get_storage_path("upstream");
    char *escname = dslink_str_escape(node->name);
    sprintf(tmp, "%s/%s", base, escname);

    dslink_free((void *) base);

    json_t *output = json_object();

    BrokerNode* propNode;
    propNode = dslink_map_get(node->children, "name")->data;
    json_object_set_nocheck(output, "name", propNode->value);

    propNode = dslink_map_get(node->children, "brokerName")->data;
    json_object_set_nocheck(output, "brokerName", propNode->value);

    propNode = dslink_map_get(node->children, "url")->data;
    json_object_set_nocheck(output, "url", propNode->value);

    propNode = dslink_map_get(node->children, "token")->data;
    json_object_set_nocheck(output, "token", propNode->value);

    propNode = dslink_map_get(node->children, "group")->data;
    json_object_set_nocheck(output, "group", propNode->value);


    propNode = dslink_map_get(node->children, "enabled")->data;
    json_object_set_nocheck(output, "enabled", propNode->value);

    json_dump_file(output, tmp, 0);
    json_decref(output);
    dslink_free(escname);
}

static
void load_upstream_node(BrokerNode *parentNode, const char* nodeName, json_t* data) {
    (void)nodeName;
// nodeName previously was upstream file's name. Above statements are commented out because broker node is already
// created in add_upstream_invoke and has no meaning
//    char* unescName = dslink_str_escape(nodeName);
//    BrokerNode *upstreamNode = broker_node_create(nodeName, "node");
//    dslink_free(unescName);
//    if (!upstreamNode) {
//        return;
//    }
    add_upstream_invoke(NULL, parentNode, data, PERMISSION_CONFIG);

}

static
int load_upstreams(BrokerNode *parentNode){
    uv_fs_t dir;

    const char *base = broker_get_storage_path("upstream");

    uv_fs_mkdir(NULL, &dir, base, 0770, NULL);

    if (uv_fs_scandir(NULL, &dir, base, 0, NULL) < 0) {
        return 0;
    }

    uv_dirent_t d;
    while (uv_fs_scandir_next(&dir, &d) != UV_EOF) {
        if (d.type != UV_DIRENT_FILE) {
            continue;
        }

        char tmp[256];
        int len = snprintf(tmp, sizeof(tmp) - 1, "%s/%s", base, d.name);
        tmp[len] = '\0';

        json_error_t err;
        json_t *val = json_load_file(tmp, 0 , &err);
        if (val) {
            load_upstream_node(parentNode, d.name, val);
            json_decref(val);
        }
    }

    dslink_free((void *) base);

    return 0;
}

void start_upstream_newsettings(BrokerNode *sysUpstreamNode) {
    // start upstream poll
    json_t* namejson = ((BrokerNode *)dslink_map_get(sysUpstreamNode->children, "name")->data)->value;
    json_t* brokerNameJson = ((BrokerNode *)dslink_map_get(sysUpstreamNode->children, "brokerName")->data)->value;
    json_t* urlJson = ((BrokerNode *)dslink_map_get(sysUpstreamNode->children, "url")->data)->value;
    json_t* groupJson = ((BrokerNode *)dslink_map_get(sysUpstreamNode->children, "group")->data)->value;

    if (json_is_string(namejson) && json_is_string(brokerNameJson)
        && json_is_string(urlJson)) {
        upstream_create_poll(json_string_value(urlJson),
                             json_string_value(namejson),
                             json_string_value(brokerNameJson),
                             json_string_value(groupJson));
    }

    sysUpstreamNode->name = dslink_strdup(json_string_value(namejson));
}

void delete_upstream(UpstreamPoll *upstreamPoll) {

    if(!upstreamPoll)
        return;

    int action = upstreamPoll->pendingDelete;
    BrokerNode *sysUpstreamNode = upstreamPoll->pendingActionNode;

    if((action == UPSTREAM_ACTION_DELETE)
       || (action == UPSTREAM_ACTION_RESET)
          || (action == UPSTREAM_ACTION_STOP)) {


        DownstreamNode *upstreamNode = upstreamPoll->node;
        upstream_clear_poll(upstreamPoll);
        dslink_free(upstreamPoll);
        upstreamNode->upstreamPoll = NULL;

        if(action == UPSTREAM_ACTION_DELETE) {
            broker_node_free((BrokerNode *) upstreamNode);
            delete_upstream_file(sysUpstreamNode->name);
            broker_node_free(sysUpstreamNode);
        }

        if(action == UPSTREAM_ACTION_RESET) {
            broker_node_free((BrokerNode *) upstreamNode);
            json_t* nameJson = json_deep_copy(((BrokerNode *)dslink_map_get(sysUpstreamNode->children, "name")->data)->value);
            json_t* brokerNameJson = json_deep_copy(((BrokerNode *)dslink_map_get(sysUpstreamNode->children, "brokerName")->data)->value);
            json_t* urlJson = json_deep_copy(((BrokerNode *)dslink_map_get(sysUpstreamNode->children, "url")->data)->value);
            json_t* groupJson = json_deep_copy(((BrokerNode *)dslink_map_get(sysUpstreamNode->children, "group")->data)->value);
            json_t* enabledJson = json_deep_copy(((BrokerNode *)dslink_map_get(sysUpstreamNode->children, "enabled")->data)->value);
            json_t* tokenJson = json_deep_copy(((BrokerNode *)dslink_map_get(sysUpstreamNode->children, "token")->data)->value);
            BrokerNode *parentNode = sysUpstreamNode->parent;

            delete_upstream_file(sysUpstreamNode->name);
            broker_node_free(sysUpstreamNode);

            BrokerNode *newUpstreamNode = add_new_upstream(parentNode,nameJson,brokerNameJson,urlJson,tokenJson,groupJson,enabledJson);
            if(newUpstreamNode) {
                save_upstream_node(newUpstreamNode);
            }
        }
    }
}

void delete_upstream_file(const char* name) {
    const char *base = broker_get_storage_path("upstream");

    char* escname = dslink_str_escape(name);
    char tmp[256];
    int len = snprintf(tmp, sizeof(tmp) - 1, "%s/%s", base, escname);
    tmp[len] = '\0';
    dslink_free(escname);
    dslink_free((void *) base);

    uv_fs_t unlink_req;
    uv_fs_unlink(NULL, &unlink_req, tmp, NULL);
}

static
void delete_upstream_invoke(RemoteDSLink *link,
                         BrokerNode *node,
                         json_t *req, PermissionLevel maxPermission) {

    (void)maxPermission;
    broker_utils_send_closed_resp(link, req, NULL);

    BrokerNode *parentNode = node->parent;

    delete_upstream_file(parentNode->name);

    Broker *broker = mainLoop->data;
    ref_t *ref = dslink_map_get(broker->upstream->children, (void*)parentNode->name);
    if (ref) {
        DownstreamNode *upstreamNode = ref->data;
        if (upstreamNode->upstreamPoll) {

            broker->pendingActionUpstreamPoll = &upstreamNode->upstreamPoll;
            upstreamNode->upstreamPoll->pendingActionNode = parentNode;
            upstreamNode->upstreamPoll->pendingDelete = UPSTREAM_ACTION_DELETE;

            DownstreamNode *currUpstreamNode = NULL;
            ref_t *refCurrLink = dslink_map_get(broker->upstream->children, (void*)link->name);
            if(refCurrLink && refCurrLink->data)
                currUpstreamNode = refCurrLink->data;


            if(currUpstreamNode && (currUpstreamNode != upstreamNode->upstreamPoll->node)) {

                delete_upstream(upstreamNode->upstreamPoll);
                broker->pendingActionUpstreamPoll = NULL;
            }
        } else {
            broker_node_free((BrokerNode *) upstreamNode);
            delete_upstream_file(parentNode->name);
            broker_node_free(parentNode);
        }
    }
}

int upstream_prop_changed(Listener * listener, void * node) {
    (void)listener;
    BrokerNode *propNode = node;
    BrokerNode *parentNode = propNode->parent;

    Broker *broker = mainLoop->data;
    ref_t *ref = dslink_map_get(broker->upstream->children, (void*)parentNode->name);
    if (ref) {
        DownstreamNode *upstreamNode = ref->data;
        if (upstreamNode->upstreamPoll) {
            broker->pendingActionUpstreamPoll = &upstreamNode->upstreamPoll;
            upstreamNode->upstreamPoll->pendingActionNode = parentNode;
            upstreamNode->upstreamPoll->pendingDelete = UPSTREAM_ACTION_RESET;
        } else { // means upstream not enabled (there is no upstreamPoll), still update upstream node with new properties
            broker_node_free((BrokerNode*)upstreamNode);
            json_t* nameJson = json_deep_copy(((BrokerNode *)dslink_map_get(parentNode->children, "name")->data)->value);
            json_t* brokerNameJson = json_deep_copy(((BrokerNode *)dslink_map_get(parentNode->children, "brokerName")->data)->value);
            json_t* urlJson = json_deep_copy(((BrokerNode *)dslink_map_get(parentNode->children, "url")->data)->value);
            json_t* groupJson = json_deep_copy(((BrokerNode *)dslink_map_get(parentNode->children, "group")->data)->value);
            json_t* enabledJson = json_deep_copy(((BrokerNode *)dslink_map_get(parentNode->children, "enabled")->data)->value);
            json_t* tokenJson = json_deep_copy(((BrokerNode *)dslink_map_get(parentNode->children, "token")->data)->value);
            BrokerNode *topUpstreamNode = parentNode->parent;
            delete_upstream_file(parentNode->name);
            broker_node_free(parentNode);

            BrokerNode *newUpstreamNode = add_new_upstream(topUpstreamNode,nameJson,brokerNameJson,urlJson,tokenJson,groupJson,enabledJson);
            if(newUpstreamNode) {
                save_upstream_node(newUpstreamNode);
            }
        }
    }
    return 0;
}

int upstream_enable_changed(Listener * listener, void * node) {
    (void)listener;
    BrokerNode *enabledNode = node;
    BrokerNode *parentNode = enabledNode->parent;
    if (json_is_false(enabledNode->value)) {
        Broker *broker = mainLoop->data;

        ref_t *ref = dslink_map_get(broker->upstream->children, (void*)parentNode->name);
        if (ref) {
            DownstreamNode *upstreamNode = ref->data;
            if (upstreamNode->upstreamPoll) {

                broker->pendingActionUpstreamPoll = &upstreamNode->upstreamPoll;
                upstreamNode->upstreamPoll->pendingActionNode = parentNode;
                upstreamNode->upstreamPoll->pendingDelete = UPSTREAM_ACTION_STOP;
            }
        }
    } else {
        // start upstream poll
        start_upstream_newsettings(parentNode);
    }
    save_upstream_node(parentNode);
    return 0;
}
BrokerNode* add_new_upstream(BrokerNode *parentNode,
                             json_t* namejson,
                             json_t* brokerNameJson,
                             json_t* urlJson,
                             json_t* tokenJson,
                             json_t* groupJson,
                             json_t* enabledJson) {

    if (!json_is_string(namejson) || !json_is_string(brokerNameJson) || !json_is_string(urlJson)) {
        return NULL;
    }
    const char *name = json_string_value(namejson);

    if (!parentNode || dslink_map_contains(parentNode->children, (void*)name)) {
        return NULL;
    }

    BrokerNode *upstreamNode = broker_node_create(name, "node");
    if (!upstreamNode) {
        return NULL;
    }

    if (broker_node_add(parentNode, upstreamNode) != 0) {
        goto fail;
    }

    BrokerNode *propNode;
    propNode = broker_node_create("name", "node");
    json_object_set_new_nocheck(propNode->meta, "$writable", json_string_nocheck("write"));
    json_object_set_new_nocheck(propNode->meta, "$type", json_string_nocheck("string"));
    broker_node_update_value(propNode, namejson, 0);
    listener_add(&propNode->on_value_update, upstream_prop_changed, propNode);
    broker_node_add(upstreamNode, propNode);

    propNode = broker_node_create("brokerName", "node");
    json_object_set_new_nocheck(propNode->meta, "$writable", json_string_nocheck("write"));
    json_object_set_new_nocheck(propNode->meta, "$type", json_string_nocheck("string"));
    broker_node_update_value(propNode, brokerNameJson, 0);
    listener_add(&propNode->on_value_update, upstream_prop_changed, propNode);
    broker_node_add(upstreamNode, propNode);


    propNode = broker_node_create("url", "node");
    json_object_set_new_nocheck(propNode->meta, "$writable", json_string_nocheck("write"));
    json_object_set_new_nocheck(propNode->meta, "$type", json_string_nocheck("string"));
    broker_node_update_value(propNode, urlJson, 0);
    listener_add(&propNode->on_value_update, upstream_prop_changed, propNode);
    broker_node_add(upstreamNode, propNode);

    propNode = broker_node_create("token", "node");
    json_object_set_new_nocheck(propNode->meta, "$writable", json_string_nocheck("write"));
    json_object_set_new_nocheck(propNode->meta, "$type", json_string_nocheck("string"));
    broker_node_update_value(propNode, tokenJson, 0);
    listener_add(&propNode->on_value_update, upstream_prop_changed, propNode);
    broker_node_add(upstreamNode, propNode);


    propNode = broker_node_create("group", "node");
    json_object_set_new_nocheck(propNode->meta, "$writable", json_string_nocheck("write"));
    json_object_set_new_nocheck(propNode->meta, "$type", json_string_nocheck("string"));
    broker_node_update_value(propNode, groupJson, 0);
    listener_add(&propNode->on_value_update, upstream_prop_changed, propNode);
    broker_node_add(upstreamNode, propNode);

    propNode = broker_node_create("enabled", "node");
    json_object_set_new_nocheck(propNode->meta, "$writable", json_string_nocheck("write"));
    json_object_set_new_nocheck(propNode->meta, "$type", json_string_nocheck("bool"));
    if (json_is_false(enabledJson)) {
        broker_node_update_value(propNode, json_false(), 0);
    } else {
        broker_node_update_value(propNode, json_true(), 0);
    }
    listener_add(&propNode->on_value_update, upstream_enable_changed, propNode);

    broker_node_add(upstreamNode, propNode);

    // TODO: detect enabled change and start/stop upstream

    BrokerNode *deleteAction = broker_node_create("delete", "node");
    json_object_set_new_nocheck(deleteAction->meta, "$invokable", json_string_nocheck("config"));
    broker_node_add(upstreamNode, deleteAction);

    deleteAction->on_invoke = delete_upstream_invoke;

    create_upstream_node(mainLoop->data, name);

    log_info("Upstream added `%s`\n", name);

    // (uv_loop_t *loop, const char *brokerUrl, const char *name, const char *idPrefix) {
    if (json_is_string(namejson) && json_is_string(brokerNameJson)
        && json_is_string(urlJson) && !json_is_false(enabledJson)) {
        upstream_create_poll(json_string_value(urlJson),
                             json_string_value(namejson), json_string_value(brokerNameJson),
                             json_string_value(groupJson));
    }

    return upstreamNode;
    fail:
    broker_node_free(upstreamNode);
    return NULL;
}
void add_upstream_invoke(RemoteDSLink *link,
                         BrokerNode *node,
                         json_t *req, PermissionLevel maxPermission) {
    (void)maxPermission;
    json_t *params = NULL;
    BrokerNode* parentNode;
    if (link) {
        // invoked
        if (req) {
            params = json_object_get(req, "params");
        }
        if (!json_is_object(params)) {
            broker_utils_send_closed_resp(link, req, "invalidParameter");
            return;
        }
        parentNode = node->parent;
    } else {
        // loaded
        params = req;
        parentNode = node;
    }

    json_t* namejson = json_object_get(params , "name");
    json_t* brokerNameJson = json_object_get(params , "brokerName");
    json_t* urlJson = json_object_get(params , "url");
    json_t* tokenJson = json_object_get(params , "token");
    json_t* groupJson = json_object_get(params , "group");
    json_t* enabledJson = json_object_get(params , "enabled");

    BrokerNode *upstreamNode = add_new_upstream(parentNode, namejson, brokerNameJson,urlJson,tokenJson,groupJson,enabledJson);

    if(upstreamNode == NULL) {
        broker_utils_send_closed_resp(link, req, "invalidParameter");
        return;
    }

    if (link) {
        // only save when it's from an action
        save_upstream_node(upstreamNode);
    }
}

//void add_upstream_invoke(RemoteDSLink *link,
//                      BrokerNode *node,
//                      json_t *req, PermissionLevel maxPermission) {
//    (void)maxPermission;
//    json_t *params = NULL;
//    BrokerNode* parentNode;
//    if (link) {
//        // invoked
//        if (req) {
//            params = json_object_get(req, "params");
//        }
//        if (!json_is_object(params)) {
//            broker_utils_send_closed_resp(link, req, "invalidParameter");
//            return;
//        }
//        parentNode = node->parent;
//    } else {
//        // loaded
//        params = req;
//        parentNode = node;
//    }
//
//    json_t* namejson = json_object_get(params , "name");
//    json_t* brokerNameJson = json_object_get(params , "brokerName");
//    json_t* urlJson = json_object_get(params , "url");
//    json_t* tokenJson = json_object_get(params , "token");
//    json_t* groupJson = json_object_get(params , "group");
//    json_t* enabledJson = json_object_get(params , "enabled");
//
//    if (!json_is_string(namejson) || !json_is_string(brokerNameJson) || !json_is_string(urlJson)) {
//        broker_utils_send_closed_resp(link, req, "invalidParameter");
//        return;
//    }
//    const char *name = json_string_value(namejson);
//
//    if (!parentNode || dslink_map_contains(parentNode->children, (void*)name)) {
//        broker_utils_send_closed_resp(link, req, "invalidParameter");
//        return;
//    }
//
//    BrokerNode *upstreamNode = broker_node_create(name, "node");
//    if (!upstreamNode) {
//        return;
//    }
//
//    if (broker_node_add(parentNode, upstreamNode) != 0) {
//        goto fail;
//    }
//
//    BrokerNode *propNode;
//    propNode = broker_node_create("name", "node");
//    json_object_set_new_nocheck(propNode->meta, "$writable", json_string_nocheck("write"));
//    json_object_set_new_nocheck(propNode->meta, "$type", json_string_nocheck("string"));
//    broker_node_update_value(propNode, namejson, 0);
//    listener_add(&propNode->on_value_update, upstream_prop_changed, propNode);
//    broker_node_add(upstreamNode, propNode);
//
//    propNode = broker_node_create("brokerName", "node");
//    json_object_set_new_nocheck(propNode->meta, "$writable", json_string_nocheck("write"));
//    json_object_set_new_nocheck(propNode->meta, "$type", json_string_nocheck("string"));
//    broker_node_update_value(propNode, brokerNameJson, 0);
//    listener_add(&propNode->on_value_update, upstream_prop_changed, propNode);
//    broker_node_add(upstreamNode, propNode);
//
//
//    propNode = broker_node_create("url", "node");
//    json_object_set_new_nocheck(propNode->meta, "$writable", json_string_nocheck("write"));
//    json_object_set_new_nocheck(propNode->meta, "$type", json_string_nocheck("string"));
//    broker_node_update_value(propNode, urlJson, 0);
//    listener_add(&propNode->on_value_update, upstream_prop_changed, propNode);
//    broker_node_add(upstreamNode, propNode);
//
//    propNode = broker_node_create("token", "node");
//    json_object_set_new_nocheck(propNode->meta, "$writable", json_string_nocheck("write"));
//    json_object_set_new_nocheck(propNode->meta, "$type", json_string_nocheck("string"));
//    broker_node_update_value(propNode, tokenJson, 0);
//    listener_add(&propNode->on_value_update, upstream_prop_changed, propNode);
//    broker_node_add(upstreamNode, propNode);
//
//
//    propNode = broker_node_create("group", "node");
//    json_object_set_new_nocheck(propNode->meta, "$writable", json_string_nocheck("write"));
//    json_object_set_new_nocheck(propNode->meta, "$type", json_string_nocheck("string"));
//    broker_node_update_value(propNode, groupJson, 0);
//    listener_add(&propNode->on_value_update, upstream_prop_changed, propNode);
//    broker_node_add(upstreamNode, propNode);
//
//    propNode = broker_node_create("enabled", "node");
//    json_object_set_new_nocheck(propNode->meta, "$writable", json_string_nocheck("write"));
//    json_object_set_new_nocheck(propNode->meta, "$type", json_string_nocheck("bool"));
//    if (json_is_false(enabledJson)) {
//        broker_node_update_value(propNode, json_false(), 0);
//    } else {
//        broker_node_update_value(propNode, json_true(), 0);
//    }
//    listener_add(&propNode->on_value_update, upstream_enable_changed, propNode);
//
//    broker_node_add(upstreamNode, propNode);
//
//    // TODO: detect enabled change and start/stop upstream
//
//    BrokerNode *deleteAction = broker_node_create("delete", "node");
//    json_object_set_new_nocheck(deleteAction->meta, "$invokable", json_string_nocheck("config"));
//    broker_node_add(upstreamNode, deleteAction);
//
//    deleteAction->on_invoke = delete_upstream_invoke;
//
//    create_upstream_node(mainLoop->data, name);
//
//    log_info("Upstream added `%s`\n", name);
//    if (link) {
//        // only save when it's from an action
//        save_upstream_node(upstreamNode);
//    }
//   // (uv_loop_t *loop, const char *brokerUrl, const char *name, const char *idPrefix) {
//    if (json_is_string(namejson) && json_is_string(brokerNameJson)
//        && json_is_string(urlJson) && !json_is_false(enabledJson)) {
//        upstream_create_poll(json_string_value(urlJson),
//                              json_string_value(namejson), json_string_value(brokerNameJson),
//                             json_string_value(groupJson));
//    }
//
//    return;
//fail:
//    broker_node_free(upstreamNode);
//}

int init_sys_upstream_node(BrokerNode *sysNode) {
    BrokerNode *upstreamNode = broker_node_create("upstream", "node");
    if (!upstreamNode) {
        return 1;
    }

    if (broker_node_add(sysNode, upstreamNode) != 0) {
        broker_node_free(upstreamNode);
        return 1;
    }


    BrokerNode *addUpstreamAction = broker_node_create("add_connection", "node");
    if (!addUpstreamAction) {
        return 1;
    }

    if (json_object_set_new_nocheck(addUpstreamAction->meta, "$invokable",
                            json_string_nocheck("config")) != 0) {
        broker_node_free(addUpstreamAction);
        return 1;
    }


    json_error_t err;
    json_t *paramList = json_loads(
            "[{\"name\":\"name\",\"type\":\"string\",\"description\":\"Upstream Broker Name\",\"placeholder\":\"UpstreamBroker\"},{\"name\":\"url\",\"type\":\"string\",\"description\":\"Url to the Upstream Broker\",\"placeholder\":\"http://upstream.broker.com/conn\"},{\"name\":\"brokerName\",\"type\":\"string\",\"description\":\"The name of the link when connected to the Upstream Broker\",\"placeholder\":\"ThisBroker\"},{\"name\":\"token\",\"type\":\"string\",\"description\":\"Broker Token (if needed)\",\"placeholder\":\"OptionalAuthToken\"},{\"name\":\"group\",\"type\":\"string\",\"description\":\"default permission group\"}]",
            0, &err);
    if (!paramList || json_object_set_new_nocheck(addUpstreamAction->meta, "$params", paramList) != 0) {
        return 1;
    }


    if (broker_node_add(upstreamNode, addUpstreamAction) != 0) {
        broker_node_free(addUpstreamAction);
        return 1;
    }

    addUpstreamAction->on_invoke = add_upstream_invoke;

    load_upstreams(upstreamNode);
    return 0;
}
