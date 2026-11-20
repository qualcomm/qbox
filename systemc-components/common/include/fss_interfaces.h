/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _GS_FSS_INTERFACES_
#define _GS_FSS_INTERFACES_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief Definition of aliases for the used types.
 */
typedef char fssChar;
typedef int32_t fssInt32;
typedef uint64_t fssUint64;
typedef double fssFloat64;

typedef const fssChar* fssString;
typedef fssInt32 fssRetStatus;
typedef fssUint64 fssBusIfItemIndex;
typedef fssUint64 fssBusIfItemSize;
typedef fssFloat64 fssTimeInSec;

/**
 * @brief Forward declaration of interfaces.
 */
struct fssBindIf;
struct fssBusIf;
struct fssConfigIf;
struct fssEventsIf;
struct fssTimeSyncIf;
struct fssControlIf;

/**
 * @brief Declaration of pointers to the interfaces.
 */
typedef fssBindIf* fssBindIFHandle;
typedef fssBusIf* fssBusIfHandle;
typedef fssConfigIf* fssConfigIfHandle;
typedef fssEventsIf* fssEventsIfHandle;
typedef fssTimeSyncIf* fssTimeSyncIfHandle;
typedef fssControlIf* fssControlIfHanlde;
typedef void* fssBusIfItemData;

/**
 * @brief Possible values for functions return status.
 *
 * FIXME: more descriptive error codes may be introduced!
 */
#define fssStatusOk    ((fssRetStatus)0)
#define fssStatusError ((fssRetStatus) - 1)

/**
 * @brief Time window struct declaration
 * 'from' and 'to' members are 64 floating point types to represent time in seconds.
 */
typedef struct {
    fssTimeInSec from;
    fssTimeInSec to;
} TimeWindow;

/******************************
 Declaration of function types
********************************/

/**
 * @brief Get configuration string associated with name.
 * This function can be called by a node in the fssCreateNode() function to get the node configurations
 * early while the node is constructed.
 *
 * @param configIfHandle pointer to the peer fssConfigIf supplying this fssGetConfigFnPtr function.
 * This parameter can be passed by the component responsible for creating the node calling fssCreateNode().
 * @param name name of the needed configurations.
 *
 * @return configuration parameters string.
 */
typedef fssString (*fssGetConfigFnPtr)(fssConfigIfHandle configIfHandle, fssString name);

/**
 * @brief signals a simulation lifecycle event from any node that has been bound to the events interface.
 * This function can be called by the peer node bound to the events interface of this node.
 *
 * @param eventsIfHandle pointer to the fssEventsIf supplying this fssEventHandlerFnPtr function.
 * @param event event number.
 * There can be a contract between the bound pair of nodes on the meaning of each event number.
 */
typedef void (*fssEventHandlerFnPtr)(fssEventsIfHandle eventsIfHandle, fssUint64 event);

/**
 * @brief Update node time based on the received time window.
 * This function can be called by the peer node bound to the time sync interface of this node.
 *
 * @param timeSyncIfHandle pointer to the fssTimeSyncIf supplying this fssUpdateTimeWindowFnPtr function.
 * @param window time window.
 */
typedef void (*fssUpdateTimeWindowFnPtr)(fssTimeSyncIfHandle timeSyncIfHandle, TimeWindow window);

/**
 * @brief Inspect or control the node, set or get the node parameters, inject memory value at specifc address,
 * or get memory value at specific address.
 * This function can be called by the peer node bound to the control interface of this node.
 *
 * @param controlIfHandle pointer to the fssControlIf supplying this fssDoCommandFnPtr function.
 * @param cmd control string i.e. json document or key-value DB
 * The node pair must agree on the format of the control string to be parsed correctly by the receiving node.
 *
 * @return requested control information string i.e. json document or key-value DB
 */
typedef fssString (*fssDoCommandFnPtr)(fssControlIfHanlde controlIfHandle, fssString cmd);

/**
 * @brief Get number of items on the bus.
 * This function can be called by the peer node bound to the data interface of this node.
 *
 * @param busIfHandle pointer to the fssBusIf supplying this fssGetNumberFnPtr function.
 *
 * @return number of items in the bus.
 */
typedef fssBusIfItemSize (*fssBusIfGetItemsNumberFnPtr)(fssBusIfHandle busIfHandle);

/**
 * @brief Get the name of an item on the bus.
 * This function can be called by the peer node bound to the data interface of this node.
 *
 * @param busIfHandle pointer to the fssBusIf supplying this fssGetNameFnPtr function.
 * @param index index of the item in the bus.
 *
 * @return name of the item in the bus.
 */
typedef fssString (*fssBusIfGetNameFromIndexFnPtr)(fssBusIfHandle busIfHandle, fssBusIfItemIndex index);

/**
 * @brief Get the size of an item on the bus.
 * This function can be called by the peer node bound to the data interface of this node.
 *
 * @param busIfHandle pointer to the fssBusIf supplying this fssGetNameFnPtr function.
 * @param index index of the item in the bus.
 *
 * @return size of the item in the bus.
 */
typedef fssBusIfItemSize (*fssBusIfGetSizeFnPtr)(fssBusIfHandle busIfHandle, fssBusIfItemIndex index);

/**
 * @brief Get the index of an item on the bus.
 * This function can be called by the peer node bound to the data interface of this node.
 *
 * @param busIfHandle pointer to the fssBusIf supplying this fssGetNameFnPtr function.
 * @param name name of the item in the bus.
 *
 * @return index of the item in the bus.
 */
typedef fssBusIfItemSize (*fssBusIfGetIndexFnPtr)(fssBusIfHandle busIfHandle, fssString name);

/**
 * @brief add an item to the bus.
 * This function can be called by the peer node bound to the data interface of this node.
 *
 * @param busIfHandle pointer to the fssBusIf supplying this fssGetNameFnPtr function.
 * @param name name of the item in the bus.
 * @param size size of the item in the bus.
 *
 * @return index of the item in the bus.
 */
typedef fssBusIfItemIndex (*fssBusIfAddItemFnPtr)(fssBusIfHandle busIfHandle, fssString name, fssBusIfItemSize size);

/**
 * @brief Get an item from the bus.
 * This function can be called by the peer node bound to the data interface of this node.
 *
 * @param busIfHandle pointer to the fssBusIf supplying this fssGetNameFnPtr function.
 * @param index index of the item in the bus.
 *
 * @return data of an item in the bus.
 */
typedef fssBusIfItemData (*fssBusIfGetItemFnPtr)(fssBusIfHandle busIfHandle, fssBusIfItemIndex index);

/**
 * @brief set an item on the bus.
 * This function can be called by the peer node bound to the data interface of this node.
 *
 * @param busIfHandle pointer to the fssBusIf supplying this fssGetNameFnPtr function.
 * @param index index of the item in the bus.
 * @param data data of an item in the bus.
 */
typedef void (*fssBusIfSetItemFnPtr)(fssBusIfHandle busIfHandle, fssBusIfItemIndex index, fssBusIfItemData data);

/**
 * @brief Transmit data.
 * This function can be called by the peer node bound to the data interface of this node.
 * The type of data access i.e. write/read and the other details of the data transmitted
 * i.e. address and size are defined by the fssBusIf.
 *
 * @param busIfHandle pointer to the fssBusIf supplying this fssTransmitFnPtr function.
 */
typedef void (*fssBusIfTransmitFnPtr)(fssBusIfHandle busIfHandle);

/**
 * @brief Bind the events interface.
 * This function can be called by the node constructor to bind the node with a peer events interface.
 *
 * @param bindIfHandle pointer to the node's fssBindIf.
 * @param name peer node's events interface name.
 * @param eventsIfHandle pointer to the peer node's fssEventsIf.
 *
 * @return return status value.
 * can be either 'fssStatusOk' on success or 'fssStatusError' on failure.
 */
typedef fssRetStatus (*fssBindEventsIfFnPtr)(fssBindIFHandle bindIfHandle, fssString name,
                                             fssEventsIfHandle eventsIfHandle);

/**
 * @brief Bind the data interface.
 * This function can be used to bind the node with a peer data interface.
 *
 * @param bindIfHandle pointer to the node's fssBindIf.
 * @param name peer node's data interface name.
 * @param busIfHandle pointer to the peer node's fssBusIf.
 *
 * @return return status value.
 * can be either 'fssStatusOk' on success or 'fssStatusError' on failure.
 */
typedef fssRetStatus (*fssBindBusIfFnPtr)(fssBindIFHandle bindIfHandle, fssString name, fssBusIfHandle busIfHandle);

/**
 * @brief Bind the time synchronization interface.
 * This function can be used to bind the node with a peer time synchronization interface.
 *
 * @param bindIfHandle pointer to the node's fssBindIf.
 * @param name peer node's time synchronization interface name.
 * @param timeSyncIfHandle pointer to the peer node's fssTimeSyncIf.
 *
 * @return return status value.
 * can be either 'fssStatusOk' on success or 'fssStatusError' on failure.
 */
typedef fssRetStatus (*fssBindTimeSyncIfFnPtr)(fssBindIFHandle bindIfHandle, fssString name,
                                               fssTimeSyncIfHandle timeSyncIfHandle);

/**
 * @brief Bind the control interface.
 * This function can be used to bind the node with a peer control interface.
 *
 * @param bindIfHandle pointer to the node's fssBindIf.
 * @param name peer node's control interface name.
 * @param controlIfHandle pointer to the peer node's fssControlIf.
 *
 * @return return status value.
 * can be either 'fssStatusOk' on success or 'fssStatusError' on failure.
 */
typedef fssRetStatus (*fssBindControlIfFnPtr)(fssBindIFHandle bindIfHandle, fssString name,
                                              fssControlIfHanlde controlIfHandle);

/**
 * @brief Get a pointer to the node's fssEventsIf.
 * This function can be used to bind the peer node with node's events interface.
 *
 * @param bindIfHandle pointer to the node's fssBindIf.
 * @param name node's events interface name.
 *
 * @return pointer to the node's fssEventsIf.
 */
typedef fssEventsIfHandle (*fssGetEventsIfHandleFnPtr)(fssBindIFHandle bindIfHandle, fssString name);

/**
 * @brief Get a pointer to the node's fssBusIf.
 * This function can be used to bind the peer node with node's data interface.
 *
 * @param bindIfHandle pointer to the node's fssBindIf.
 * @param name node's data interface name.
 *
 * @return pointer to the node's fssBusIf.
 */
typedef fssBusIfHandle (*fssGetBusIfFnPtr)(fssBindIFHandle bindIfHandle, fssString name);

/**
 * @brief Get a pointer to the node's fssConfigIf.
 * This function can be used to bind the peer node with node's config interface.
 *
 * @param bindIfHandle pointer to the node's fssBindIf.
 * @param name node's config interface name.
 *
 * @return pointer to the node's fssConfigIf.
 */
typedef fssConfigIfHandle (*fssGetConfigIfFnPtr)(fssBindIFHandle bindIfHandle, fssString name);

/**
 * @brief Get a pointer to the node's fssTimeSyncIf.
 * This function can be used to bind the peer node with node's time synchronization
 * interface.
 *
 * @param bindIfHandle pointer to the node's fssBindIf.
 * @param name node's time synchronization interface name.
 *
 * @return pointer to the node's fssTimeSyncIf.
 */
typedef fssTimeSyncIfHandle (*fssGetTimeSyncIfHandleFnPtr)(fssBindIFHandle bindIfHandle, fssString name);

/**
 * @brief Get a pointer to the node's fssControlIf.
 * This function can be used to bind the peer node with node's control interface.
 *
 * @param bindIfHandle pointer to the node's fssBindIf.
 * @param name node's control interface name.
 *
 * @return pointer to the node's fssControlIf.
 */
typedef fssControlIfHanlde (*fssGetControlIfHandleFnPtr)(fssBindIFHandle bindIfHandle, fssString name);

/******************************
 Declaration of interfaces
********************************/

/**
 * @brief Bus Interface encapsulates the data which can be transmitted on a bus.
 */
typedef struct fssBusIf {
    fssUint64 version;
    fssBusIfGetItemsNumberFnPtr getNumber;
    fssBusIfGetNameFromIndexFnPtr getName;
    fssBusIfGetSizeFnPtr getSize;
    fssBusIfGetIndexFnPtr getIndex;
    fssBusIfAddItemFnPtr addItem;
    fssBusIfGetItemFnPtr getItem;
    fssBusIfSetItemFnPtr setItem;
    fssBusIfTransmitFnPtr transmit;
} fssBusIf;

/**
 * @brief Configuration interface encapsulates the function to get configurations.
 */
typedef struct fssConfigIf {
    fssUint64 version;
    fssGetConfigFnPtr getConfigs;
} fssConfigIf;

/**
 * @brief Events interface encapsulates the function to handle simulation events.
 */
typedef struct fssEventsIf {
    fssUint64 version;
    fssEventHandlerFnPtr handleEvents;
} fssEventsIf;

/**
 * @brief Time synchronization interface encapsulates the function to handle time window update.
 */
typedef struct fssTimeSyncIf {
    fssUint64 version;
    fssUpdateTimeWindowFnPtr updateTimeWindow;
} fssTimeSyncIf;

/**
 * @brief Control interface encapsulates the function to inject and query parameters.
 */
typedef struct fssControlIf {
    fssUint64 version;
    fssDoCommandFnPtr doCommand;
} fssControlIf;

/**
 * @brief Bind interface encapsulates the functions to bind different interfaces.
 */
typedef struct fssBindIf {
    fssUint64 version;
    fssBindEventsIfFnPtr bindEventsIf;
    fssBindBusIfFnPtr bindBusIf;
    fssBindTimeSyncIfFnPtr bindTimeSyncIf;
    fssBindControlIfFnPtr bindControlIf;
    fssGetEventsIfHandleFnPtr getEventsIfHandle;
    fssGetBusIfFnPtr getBusIfHandle;
    fssGetTimeSyncIfHandleFnPtr getTimeSyncIfHandle;
    fssGetControlIfHandleFnPtr getControlIfHandle;
} fssBindIf;

#if defined _WIN32 || defined __CYGWIN__
#define fssExport __attribute__((dllexport))
#else
#if __GNUC__ >= 4
#define fssExport __attribute__((visibility("default")))
#else
#define fssExport
#endif
#endif

/**
 * @brief Create a new node and return a pointer to the fssBindIf of the node.
 *
 * @param nodeName name of the node.
 * @param configIfHandle pointer to a fssConfigIf.
 * This parameter is used to call the enclosed fssGetConfigFnPtr function member of the fssConfigIf to get the
 * configurations early while the node is constructed.
 * @return pointer to the fssBindIf of the created node.
 */
fssExport fssBindIFHandle fssCreateNode(fssString nodeName, fssConfigIfHandle configIfHandle);

/**
 * @brief Destroy a node.
 *
 * @param bindIfHandle pointer to the fssBindIf of the node.
 *
 * @return return status value.
 * can be either 'fssStatusOk' on success or 'fssStatusError' on failure.
 */
fssExport fssRetStatus fssDestroyNode(fssBindIFHandle bindIfHandle);

#ifdef __cplusplus
}
#endif

#endif
