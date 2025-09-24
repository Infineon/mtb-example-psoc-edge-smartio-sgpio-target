/*****************************************************************************
* File Name  : sgpio_target.c
*
* Description: This file contains function definitions for implementing the
*              SGPIO Target interface.
*
*
*******************************************************************************
* Copyright 2023-2025, Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
*
* This software, including source code, documentation and related
* materials ("Software") is owned by Cypress Semiconductor Corporation
* or one of its affiliates ("Cypress") and is protected by and subject to
* worldwide patent protection (United States and foreign),
* United States copyright laws and international treaty provisions.
* Therefore, you may use this Software only as provided in the license
* agreement accompanying the software package from which you
* obtained this Software ("EULA").
* If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
* non-transferable license to copy, modify, and compile the Software
* source code solely for use in connection with Cypress's
* integrated circuit products.  Any reproduction, modification, translation,
* compilation, or representation of this Software except as specified
* above is prohibited without the express written permission of Cypress.
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
* reserves the right to make changes to the Software without notice. Cypress
* does not assume any liability arising out of the application or use of the
* Software or any product or circuit described in the Software. Cypress does
* not authorize its products for use in any products where a malfunction or
* failure of the Cypress product may reasonably be expected to result in
* significant property damage, injury or death ("High Risk Product"). By
* including Cypress's product in a High Risk Product, the manufacturer
* of such system or application assumes all risk of such use and in doing
* so agrees to indemnify Cypress against all liability.
*******************************************************************************/

#include "sgpio_target.h"

/*******************************************************************************
* Function Name: sgpio_target_init
********************************************************************************
* Summary:
*  Initialize the SGPIO block.
*
* Parameters:
*  spi_base     The pointer to the SPI.
*  smartio_base The pointer to the Smart I/O.
*  config       The pointer to the configuration structure
*  context      The pointer to the context structure stc_sgpio_target_context_t
*               allocated by the user. The structure is used during the SGPIO
*               operation for internal configuration and data retention. The
*               user must not modify anything in this structure.
*
* Return:
*  en_sgpio_target_status_t
*               Returns the status of this operation.
*
*******************************************************************************/
en_sgpio_target_status_t sgpio_target_init(CySCB_Type *spi_base,
        SMARTIO_PRT_Type *smartio_base,
        stc_sgpio_target_config_t const *config,
        stc_sgpio_target_context_t *context)
{
    if (NULL == context || NULL == spi_base || NULL == smartio_base)
    {
        return SGPIO_TARGET_BAD_PARAM;
    }

    /* Set the frame sizes */
    context->bit_frame_size = config->num_drives*SGPIO_TARGET_BITS_PER_DRIVE;
    context->byte_frame_size = context->bit_frame_size / 8;
    /* Check if multiple of 8. If not, add an extra byte to the frame */
    if (0 != (context->bit_frame_size % 8))
    {
        context->byte_frame_size++;
    }

    /* Check for the maximum frame size */
    if (SGPIO_TARGET_MAX_FRAME_SIZE < context->byte_frame_size)
    {
        return SGPIO_TARGET_BAD_PARAM;
    }

    /* Set SPI width */
    if (16 >= context->bit_frame_size)
    {
        context->spi_width = context->bit_frame_size;
    }
    else if (32 >= context->bit_frame_size)
    {
        context->spi_width = context->bit_frame_size/2;
    }
    else if (48 >= context->bit_frame_size)
    {
        context->spi_width = context->bit_frame_size/3;
    }
    else
    {
        context->spi_width = context->bit_frame_size/4;
    }

    /* Set pointer to the hardware blocks */
    context->spi_base = spi_base;
    context->smartio_base = smartio_base;

    /* Init the Smart I/O block */
    Cy_SmartIO_Init(smartio_base, &SMARTIO_config);

    /* Init the SPI block */
    Cy_SCB_SPI_Init(context->spi_base, &SGPIO_TARGET_SPI_config, NULL);

    /* Clear remaining variables */
    context->callback = NULL;
    context->builder_count = 0;
    for (uint32_t i = 0; i < SGPIO_TARGET_MAX_FRAME_SIZE; i++)
    {
        context->sdout_data[i] = 0;
        context->sdin_data[i] = 0;
        context->scratch_sdout[i] = 0;
        context->scratch_sdin[i] = 0;
    }
    context->has_data = false;

    return SGPIO_TARGET_SUCCESS;
}

/*******************************************************************************
* Function Name: sgpio_target_deinit
********************************************************************************
* Summary:
*  De-initialize the SGPIO block.
*
* Parameters:
*  context   The pointer to the context structure stc_sgpio_target_context_t
*            allocated by the user. The structure is used during the SGPIO
*            operation for internal configuration and data retention. The user
*            must not modify anything in this structure.
*
* Return:
*  void
*
*******************************************************************************/
void sgpio_target_deinit(stc_sgpio_target_context_t *context)
{
    context->callback = NULL;
    context->builder_count = 0;

    for (uint32_t i = 0; i < SGPIO_TARGET_MAX_FRAME_SIZE; i++)
    {
        context->sdout_data[i] = 0;
        context->sdin_data[i] = 0;
        context->scratch_sdout[i] = 0;
        context->scratch_sdin[i] = 0;
    }
}

/*******************************************************************************
* Function Name: sgpio_target_enable
********************************************************************************
* Summary:
*  Enable the SGPIO block. An interrupt is triggered on the background.
*
* Parameters:
*  context   The pointer to the context structure stc_sgpio_target_context_t
*            allocated by the user. The structure is used during the SGPIO
*            operation for internal configuration and data retention. The user
*            must not modify anything in this structure.
*
* Return:
*  void
*
*******************************************************************************/
void sgpio_target_enable(stc_sgpio_target_context_t *context)
{
    context->builder_count = 0;

    /* Enable SPI and Smart I/0 */
    Cy_SmartIO_Enable(context->smartio_base);
    Cy_SCB_SPI_Enable(context->spi_base);
}

/*******************************************************************************
* Function Name: sgpio_target_disable
********************************************************************************
* Summary:
*  Disable the SGPIO block.
*
* Parameters:
*  context   The pointer to the context structure stc_sgpio_target_context_t
*            allocated by the user. The structure is used during the SGPIO
*            operation for internal configuration and data retention. The user
*            must not modify anything in this structure.
*
* Return:
*  void
*
*******************************************************************************/
void sgpio_target_disable(stc_sgpio_target_context_t *context)
{
    /* Enable SPI and Smart I/0 */
    Cy_SCB_SPI_Disable(context->spi_base, NULL);
    Cy_SmartIO_Disable(context->smartio_base);
}

/*******************************************************************************
* Function Name: sgpio_target_register_callback
********************************************************************************
* Summary:
*  Register a callback to be executed when a frame is ready.
*
* Parameters:
*  callback  The pointer to the callback function
*  context   The pointer to the context structure stc_sgpio_target_context_t
*            allocated by the user. The structure is used during the SGPIO
*            operation for internal configuration and data retention. The user
*            must not modify anything in this structure.
*
* Return:
*  void
*
*******************************************************************************/
void sgpio_target_register_callback(cb_sgpio_target_callback_t callback,
        stc_sgpio_target_context_t *context)
{
    context->callback = callback;
}

/*******************************************************************************
* Function Name: sgpio_target_has_data
********************************************************************************
* Summary:
*  Check if any data is available. The flag is cleared on read.
*
* Parameters:
*  context   The pointer to the context structure stc_sgpio_target_context_t
*            allocated by the user. The structure is used during the SGPIO
*            operation for internal configuration and data retention. The user
*            must not modify anything in this structure.
*
* Return:
*  bool      Returns true if it has data, otherwise false.
*
*******************************************************************************/
bool sgpio_target_has_data(stc_sgpio_target_context_t *context)
{
    bool ret = context->has_data;
    int32_t status;

    status = CyEnterCriticalSection();
    context->has_data = false;
    CyExitCriticalSection(status);

    return ret;
}

/*******************************************************************************
* Function Name: sgpio_target_clear
********************************************************************************
* Summary:
*  Clear internal FIFOs.
*
* Parameters:
*  context   The pointer to the context structure stc_sgpio_target_context_t
*            allocated by the user. The structure is used during the SGPIO
*            operation for internal configuration and data retention. The user
*            must not modify anything in this structure.
*
* Return:
*  void
*
*******************************************************************************/
void sgpio_target_clear(stc_sgpio_target_context_t *context)
{
    Cy_SCB_SPI_ClearRxFifo(context->spi_base);
    Cy_SCB_SPI_ClearTxFifo(context->spi_base);
}

/*******************************************************************************
* Function Name: sgpio_target_set_num_drives
********************************************************************************
*  Not implemented yet.
*
* Parameters:
*  context   The pointer to the context structure stc_sgpio_target_context_t
*            allocated by the user. The structure is used during the SGPIO
*            operation for internal configuration and data retention. The user
*            must not modify anything in this structure.
*
*******************************************************************************/
en_sgpio_target_status_t sgpio_target_set_num_drives(stc_sgpio_target_context_t *context)
{
    /* TODO */
    return SGPIO_TARGET_SUCCESS;
}

/*******************************************************************************
* Function Name: sgpio_target_read
********************************************************************************
* Summary:
*  Read the last SDataOut bits available. The least significant bits from the 
*  bus are placed in the last element of the frame array.
*  Below is an example of a 12-bit frame (frame = {X, Y}).
*  | SRAM  |Y.0 |Y.1 |Y.2 |Y.3 |Y.4 |Y.5 |Y.6 |Y.7 |X.0 |X.1 | X.2 | X.3 |
*  | Bits  |Bit0|Bit1|Bit2|Bit3|Bit4|Bit5|Bit6|Bit7|Bit8|Bit9|Bit10|Bit11|
*  | SGPIO |D0.0|D0.1|D0.2|D1.0|D1.1|D1.2|D2.0|D2.1|D2.2|D3.0|D3.1 |D3.2 |
*
* Parameters:
*  frame     Pointer to the data array to store the SDout data. The size depends
*            on the number of drives.
*  context   The pointer to the context structure stc_sgpio_target_context_t
*            allocated by the user. The structure is used during the SGPIO
*            operation for internal configuration and data retention. The user
*            must not modify anything in this structure.
*
* Return:
*  void
*
*******************************************************************************/
void sgpio_target_read(uint8_t *sdout_data, stc_sgpio_target_context_t *context)
{
    int32_t status;

    status = CyEnterCriticalSection();

    for (uint32_t i = 0; i < context->byte_frame_size; i++)
    {
        sdout_data[i] = context->sdout_data[i];
    }

    CyExitCriticalSection(status);
}

/*******************************************************************************
* Function Name: sgpio_target_write
********************************************************************************
* Summary:
*  Setup the SDataIn bits to be serialized. It will write constantly to the bus
*  as long the block is enabled. The first elements from the frame array are
*  placed in the most significant bits on the bus.
*  Below is an example of a 12-bit frame (frame = {X, Y}).
*  | SRAM  |Y.0 |Y.1 |Y.2 |Y.3 |Y.4 |Y.5 |Y.6 |Y.7 |X.0 |X.1 | X.2 | X.3 |
*  | Bits  |Bit0|Bit1|Bit2|Bit3|Bit4|Bit5|Bit6|Bit7|Bit8|Bit9|Bit10|Bit11|
*  | SGPIO |D0.0|D0.1|D0.2|D1.0|D1.1|D1.2|D2.0|D2.1|D2.2|D3.0|D3.1 |D3.2 |
*
* Parameters:
*  frame     Pointer to the data array with the frame. The size depends on the
*            number of drives.
*  context   The pointer to the context structure stc_sgpio_target_context_t
*            allocated by the user. The structure is used during the SGPIO
*            operation for internal configuration and data retention. The user
*            must not modify anything in this structure.
*
* Return:
*  void
*
*******************************************************************************/
void sgpio_target_write(uint8_t *sdin_data, stc_sgpio_target_context_t *context)
{
    int32_t status;

    status = CyEnterCriticalSection();

    for (uint32_t i = 0; i < context->byte_frame_size; i++)
    {
        context->sdin_data[i] = sdin_data[i];
    }

    CyExitCriticalSection(status);
}

/*******************************************************************************
* Function Name: sgpio_target_interrupt
********************************************************************************
* Summary
*  SGPIO Interrupt handler. It builds the SDataIn and SDataOut bits.
*
* Parameters:
*  context   The pointer to the context structure stc_sgpio_target_context_t
*            allocated by the user. The structure is used during the SGPIO
*            operation for internal configuration and data retention. The user
*            must not modify anything in this structure.
*
* Return:
*  void
*
*******************************************************************************/
void sgpio_target_interrupt(stc_sgpio_target_context_t *context)
{
    uint16_t sdataout;
    volatile uint16_t sdatain;
    uint32_t status = Cy_SCB_GetRxInterruptStatus(context->spi_base);

    /* Process the SPI Interrupt */
    if (0UL != (CY_SCB_RX_INTR_NOT_EMPTY & status))
    {
        /* Read one word from the SPI RX FIFO */
        sdataout = Cy_SCB_SPI_Read(context->spi_base);

        /* Set the 8 MSB bits for data out*/
        sdatain = (context->scratch_sdin[context->builder_count]) << 8;

        /* Get the 8 MSB bits for data in */
        context->scratch_sdout[context->builder_count++] = CY_HI8(sdataout);

        /* Set/Get the 8 LSB bits, if any */
        if (8 < context->spi_width)
        {
            /* Set the 8 LSB bits for data out */
            sdatain |= context->scratch_sdin[context->builder_count];

            /* Get the 8 LSB bits for data in */
            context->scratch_sdout[context->builder_count++] = CY_LO8(sdataout);
        }

        /* Write to the SPI TX FIFO */
        Cy_SCB_SPI_Write(context->spi_base, sdatain);

        /* Check if read all bytes of a frame */
        if (context->builder_count >= context->byte_frame_size)
        {
            /* Change State to Ready */
            context->has_data = true;

            /* Copy data from scratch to the SDOUT data */
            if (context->byte_frame_size == 2)
            {
                memcpy((uint8_t *) context->sdout_data, context->scratch_sdout, context->byte_frame_size);
            }
            else if (context->byte_frame_size == 3)
            {
                context->sdout_data[0]  = (context->scratch_sdout[2] << (context->spi_width - 8));
                context->sdout_data[0] |= (context->scratch_sdout[3] >> (16 - context->spi_width));
                context->sdout_data[1]  = (context->scratch_sdout[3] << (context->spi_width - 8));
                context->sdout_data[1] |= (context->scratch_sdout[0]);
                context->sdout_data[2]  = (context->scratch_sdout[1]);
            }
            else if (context->byte_frame_size == 4)
            {
                /* TODO */
            }
            else if (context->byte_frame_size == 5)
            {
                context->sdout_data[0]  = (context->scratch_sdout[4]);
                context->sdout_data[1]  = (context->scratch_sdout[5]);
                context->sdout_data[2]  = (context->scratch_sdout[2] << (context->spi_width - 8));
                context->sdout_data[2] |= (context->scratch_sdout[3] >> (16 - context->spi_width));
                context->sdout_data[3]  = (context->scratch_sdout[3] << (context->spi_width - 8));
                context->sdout_data[3] |= (context->scratch_sdout[0]);
                context->sdout_data[4]  = (context->scratch_sdout[1]);
            }
            else if (context->byte_frame_size == 6)
            {
                context->sdout_data[0]  = (context->scratch_sdout[4]);
                context->sdout_data[1]  = (context->scratch_sdout[5]);
                context->sdout_data[2]  = (context->scratch_sdout[2]);
                context->sdout_data[3]  = (context->scratch_sdout[3]);
                context->sdout_data[4]  = (context->scratch_sdout[0]);
                context->sdout_data[5]  = (context->scratch_sdout[1]);
            }

            /* Clear build counter */
            context->builder_count = 0;

            /* Copy data from SDataIn to scratch */
            if (context->byte_frame_size == 2)
            {
                memcpy(context->scratch_sdin, (uint8_t *) context->sdin_data, context->byte_frame_size);
            }
            else if (context->byte_frame_size == 3)
            {
                context->scratch_sdin[0]  = (context->sdin_data[0] >> (context->spi_width - 8));
                context->scratch_sdin[1]  = (context->sdin_data[0] << (16 - context->spi_width));
                context->scratch_sdin[1] |= (context->sdin_data[1] >> (context->spi_width - 8));
                context->scratch_sdin[2]  = (context->sdin_data[1]);
                context->scratch_sdin[3]  = (context->sdin_data[2]);
            }
            else if (context->byte_frame_size == 4)
            {
                /* TODO */
            }
            else if (context->byte_frame_size == 5)
            {
                context->scratch_sdin[0]  = (context->sdin_data[1]);
                context->scratch_sdin[1]  = (context->sdin_data[2]);
                context->scratch_sdin[1] |= (context->sdin_data[1] >> (context->spi_width - 8));
                context->scratch_sdin[2]  = (context->sdin_data[0] >> (context->spi_width - 8));
                context->scratch_sdin[3]  = (context->sdin_data[0] << (16 - context->spi_width));
                context->scratch_sdin[3] |= (context->sdin_data[1] >> (context->spi_width - 8));
                context->scratch_sdin[4]  = (context->sdin_data[3] >> (context->spi_width - 8));
                context->scratch_sdin[5]  = (context->sdin_data[3] << (16 - context->spi_width));
                context->scratch_sdin[5] |= (context->sdin_data[4] >> (context->spi_width - 8));
            }
            else if (context->byte_frame_size == 6)
            {
                context->scratch_sdin[0]  = (context->sdin_data[2]);
                context->scratch_sdin[1]  = (context->sdin_data[3]);
                context->scratch_sdin[2]  = (context->sdin_data[0]);
                context->scratch_sdin[3]  = (context->sdin_data[1]);
                context->scratch_sdin[4]  = (context->sdin_data[4]);
                context->scratch_sdin[5]  = (context->sdin_data[5]);
            }

            if (context->callback != NULL)
            {
                context->callback();
            }
        }
    }

    Cy_SCB_ClearRxInterrupt(context->spi_base, status);
}

/* [] END OF FILE */