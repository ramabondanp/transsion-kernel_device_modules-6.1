// SPDX-FileCopyrightText: 2024 Infineon Technologies AG
//
// SPDX-License-Identifier: MIT

/**
 * @file   authon_config.h
 * @date   January, 2021
 * @brief  SDK configuration
 *
 */
#ifndef AUTHON_CONFIG_H_
#define AUTHON_CONFIG_H_

/**
* @brief Authenticate On default device address
*/
#define ON_DEFAULT_ADDRESS (0x30)

/**
* @brief Size of UID in bytes
*/
#define UID_SIZE_BYTE (12)

/**
* @brief Number of expected devices on SWI bus
*/
#define CONFIG_SWI_MAX_COUNT \
	(4) /*!  Configure the number of maximum number of devices that can be enumerated on the SWI bus. */
#if (CONFIG_SWI_MAX_COUNT == 0)
#pragma message("Error: Invalid device expected")
#endif

/**
* @brief System configuration
*/
#define ENABLE_SWI_SEARCH \
	(0) /*!  Set "1" to enable SWI search algorithm for single or multiple devices on SWI bus. Set "0" to disable search on the bus(For direct single device only) */
#define ENABLE_DEBUG_PRINT \
	(1) /*! Set "1" to enable basic debug printing. Set "0" disable debug print. */

#endif /* AUTHON_CONFIG_H_ */
