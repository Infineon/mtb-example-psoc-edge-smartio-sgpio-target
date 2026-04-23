/*******************************************************************************
 * File Name:   main.c
 *
 * Description: This is the source code for the Smart I/O SGPIO Target in CM33
 *              non-secure environment.
 *
 * Related Document: See README.md
 *
 ********************************************************************************
 * (c) 2023-2026, Infineon Technologies AG, or an affiliate of Infineon
 * Technologies AG. All rights reserved.
 * This software, associated documentation and materials ("Software") is
 * owned by Infineon Technologies AG or one of its affiliates ("Infineon")
 * and is protected by and subject to worldwide patent protection, worldwide
 * copyright laws, and international treaty provisions. Therefore, you may use
 * this Software only as provided in the license agreement accompanying the
 * software package from which you obtained this Software. If no license
 * agreement applies, then any use, reproduction, modification, translation, or
 * compilation of this Software is prohibited without the express written
 * permission of Infineon.
 *
 * Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE
 * IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING, BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF
 * THIRD-PARTY RIGHTS AND IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A
 * SPECIFIC USE/PURPOSE OR MERCHANTABILITY.
 * Infineon reserves the right to make changes to the Software without notice.
 * You are responsible for properly designing, programming, and testing the
 * functionality and safety of your intended application of the Software, as
 * well as complying with any legal requirements related to its use. Infineon
 * does not guarantee that the Software will be free from intrusion, data theft
 * or loss, or other breaches ("Security Breaches"), and Infineon shall have
 * no liability arising out of any Security Breaches. Unless otherwise
 * explicitly approved by Infineon, the Software may not be used in any
 * application where a failure of the Product or any consequences of the use
 * thereof can reasonably be expected to result in personal injury.
 ******************************************************************************/

/*******************************************************************************
* Header Files
*******************************************************************************/
#include "retarget_io_init.h"
#include "cycfg.h"
#include <inttypes.h>
#include <string.h>
#include "sgpio_target.h"
#include "cy_gpio.h"

/*******************************************************************************
* Macros
*******************************************************************************/
#define GET_BIT(value, bit)                 ((value >> bit) & 0x1U)
#define ALL_DRIVES_ACTIVE                   (0x492U)
#define ALL_DRIVES_ERROR                    (0x924U)
#define DRIVE_NUM_TOTAL                     (4U)
#define SGPIO_TARGET_PRIORITY               (7U)
#define USER_BUTTON_ISR_PRIORITY            (7U)
#define PORT_INTR_MASK                      (0x00000001UL << 8)
#define MASKED_TRUE                         (1U)
#define FRAME_OFFSET_0                      (0U)
#define FRAME_OFFSET_1                      (1U)
#define SHIFT_AMT_8_BIT                     (8U)
#define DEFAULT_FRAME_DATA                  (0U)
#define DEBOUNCE_DELAY_MS                   (300U)

/* The timeout value in microsecond used to wait for the CM55 core to be booted.
 * Use value 0U for infinite wait till the core is booted successfully.
 */
#define CM55_BOOT_WAIT_TIME_USEC            (10U)

/* App boot address for CM55 project */
#define CM55_APP_BOOT_ADDR                  (CYMEM_CM33_0_m55_nvm_START + \
                                                CYBSP_MCUBOOT_HEADER_SIZE)

/*******************************************************************************
* Global Variables
*******************************************************************************/
static stc_sgpio_target_context_t sgpio_target_context;
static cy_stc_scb_spi_context_t   sgpio_initiator_context;

/* SGPIO Configuration */
stc_sgpio_target_config_t sgpio_config = 
{
    .num_drives = DRIVE_NUM_TOTAL,
};

volatile bool button_pressed = false;

/*******************************************************************************
* Function Name: check_status
********************************************************************************
* Summary:
*  Handler function which indicates error by switching ON the USER LED1 
*  and prints the error message on the debug UART console.
*
* Parameters:
*  message - message to print if status is non-zero.
*  status - status for evaluation.
*
* Return:
*  void
*******************************************************************************/
static void check_status(char *message, uint32_t status)
{
    if (0u != status)
    {
        printf("\r\n=====================================================\r\n");
        printf("\nFAIL: %s\r\n", message);
        printf("Error Code: 0x%08"PRIX32"\n", status);
        printf("\r\n=====================================================\r\n");

        /* On failure, turn the LED ON */
        Cy_GPIO_Write(CYBSP_USER_LED1_PORT, CYBSP_USER_LED1_PIN, CYBSP_LED_STATE_ON);
        while(true); /* Wait forever here when error occurs. */
    }
}

/*******************************************************************************
* Function Name: sgpio_interrupt_handler
********************************************************************************
* Summary:
*  Serial general-purpose input/output (SGPIO) ISR handler. 
*  Handles the SGPIO frame.
*
* Parameters:
*  void
*
* Return:
*  void
*******************************************************************************/
static void sgpio_interrupt_handler(void)
{
    sgpio_target_interrupt(&sgpio_target_context);
}

/*******************************************************************************
* Function Name: user_button_interrupt_handler
********************************************************************************
* Summary:
*   Button callback. Set a flag to be processed in the main loop.
*
* Parameters:
*   void
*
* Return:
*   void
*
*******************************************************************************/
static void user_button_interrupt_handler(void)
{
    /* USER_BTN debounce delay */
    Cy_SysLib_Delay(DEBOUNCE_DELAY_MS);

    /* Get interrupt cause */
    uint32_t intrSrc = Cy_GPIO_GetInterruptCause0();

    /* Check if the interrupt was from the user button's port and pin */
    if((PORT_INTR_MASK == (intrSrc & PORT_INTR_MASK)) &&
            (MASKED_TRUE == Cy_GPIO_GetInterruptStatusMasked(CYBSP_USER_BTN1_PORT,
                    CYBSP_USER_BTN1_PIN)))
    {
        /* Set the button status */
        button_pressed = true;

        /* Clear the interrupt */
        Cy_GPIO_ClearInterrupt(CYBSP_USER_BTN1_PORT, CYBSP_USER_BTN1_PIN);
        NVIC_ClearPendingIRQ(CYBSP_USER_BTN1_IRQ);
    }

}

/*******************************************************************************
* Function Name: main
********************************************************************************
* Summary:
* The main function for the CM33 non-secure application.
* It does...
*
*  Initialization:
*  - Initializes all the hardware blocks
*  Do forever loop:
*  - Check if SGPIO Target data is available.
*  - Check if button is pressed, if yes, print SGPIO data
*  - Check if SGPIO Initiator FIFO, if not full, write data to the FIFO
*
* Parameters:
*  void
*
* Return:
*  int
*
*******************************************************************************/
int main(void)
{
    cy_rslt_t result;
    uint16_t frame_word;
    uint8_t frame[SGPIO_TARGET_MAX_FRAME_SIZE] = {0x0AU, 0xBCU};

    cy_stc_sysint_t user_btn_int_cfg =
    {
        .intrSrc          = CYBSP_USER_BTN1_IRQ,
        .intrPriority     = USER_BUTTON_ISR_PRIORITY,
    };

    cy_stc_sysint_t sgpio_target_int_cfg =
    {
        .intrSrc          = SGPIO_TARGET_SPI_IRQ,
        .intrPriority     = SGPIO_TARGET_PRIORITY,
    };

    /* Initialize the device and board peripherals */
    result = cybsp_init();
    if (CY_RSLT_SUCCESS != result)
    {
        handle_app_error();
    }

    /* Initialize retarget-io middleware */
    init_retarget_io();

    /* \x1b[2J\x1b[;H - ANSI ESC sequence for clear screen */
    printf("\x1b[2J\x1b[;H");

    printf("******** PSOC Edge MCU: Smart I/O SGPIO Target ********\r\n\n");

    __enable_irq();

    /* Clear GPIO and NVIC interrupt before initializing to avoid false
     * triggering.
     */
    Cy_GPIO_ClearInterrupt(CYBSP_USER_BTN1_PORT, CYBSP_USER_BTN1_PIN);
    Cy_GPIO_ClearInterrupt(CYBSP_USER_BTN2_PORT, CYBSP_USER_BTN2_PIN);
    NVIC_ClearPendingIRQ(CYBSP_USER_BTN1_IRQ);

    /* Enable User-Button Interrupt. */
    Cy_SysInt_Init(&user_btn_int_cfg, &user_button_interrupt_handler);

    NVIC_EnableIRQ(user_btn_int_cfg.intrSrc);

    /* Setup SPI Interrupt */
    Cy_SysInt_Init(&sgpio_target_int_cfg, &sgpio_interrupt_handler);

    NVIC_EnableIRQ(sgpio_target_int_cfg.intrSrc);

    /* Initialize the SPI acting as SGPIO Initiator */
    result = Cy_SCB_SPI_Init(SGPIO_INITIATOR_SPI_HW, &SGPIO_INITIATOR_SPI_config,
            &sgpio_initiator_context);

    check_status("SPI init failed for SPI initiator.\r\n", result);

    /* Enable the SPI acting as SGPIO Initiator. */
    Cy_SCB_SPI_Enable(SGPIO_INITIATOR_SPI_HW);

    /* Initialize the SGPIO Target Instance. */
    result = sgpio_target_init(SGPIO_TARGET_SPI_HW, SMARTIO_HW, &sgpio_config, &sgpio_target_context);

    check_status("SGPIO Target init failed.\r\n", result);

    printf("SGPIO HW Initialization successful.\r\n\n");

    /* Enable the SGPIO Target Instance. */
    sgpio_target_enable(&sgpio_target_context);

    /* Set SGPIO output data */
    sgpio_target_write(frame, &sgpio_target_context);

    /* Clear frame data. */
    memset(frame, DEFAULT_FRAME_DATA, SGPIO_TARGET_MAX_FRAME_SIZE);

    /* Enable CM55. */
    /* CM55_APP_BOOT_ADDR must be updated if CM55 memory layout is changed.*/
    Cy_SysEnableCM55(MXCM55, CM55_APP_BOOT_ADDR, CM55_BOOT_WAIT_TIME_USEC);

    for (;;)
    {
        /* Turn LED on */
        Cy_GPIO_Write(CYBSP_USER_LED1_PORT, CYBSP_USER_LED1_PIN, CYBSP_LED_STATE_ON);

        if (sgpio_target_has_data(&sgpio_target_context))
        {
            sgpio_target_read(frame, &sgpio_target_context);

            /* Convert the frame to a 16-bit word */
            frame_word = (frame[FRAME_OFFSET_0] << SHIFT_AMT_8_BIT) | frame[FRAME_OFFSET_1];

            /* Prints frame info only when button is pressed */
            if (true == button_pressed)
            {
                printf("| Drive No | Active | Locate | Error |\n\r");
                printf("|----------|--------|--------|-------|\n\r");
                printf("| Drive 0  |    %d   |    %d   |   %d   |\n\r", GET_BIT(frame_word, 0),
                        GET_BIT(frame_word, 1),
                        GET_BIT(frame_word, 2));
                printf("| Drive 1  |    %d   |    %d   |   %d   |\n\r", GET_BIT(frame_word, 3),
                        GET_BIT(frame_word, 4),
                        GET_BIT(frame_word, 5));
                printf("| Drive 2  |    %d   |    %d   |   %d   |\n\r", GET_BIT(frame_word, 6),
                        GET_BIT(frame_word, 7),
                        GET_BIT(frame_word, 8));
                printf("| Drive 3  |    %d   |    %d   |   %d   |\n\r", GET_BIT(frame_word, 9),
                        GET_BIT(frame_word, 10),
                        GET_BIT(frame_word, 11));
                printf("--------------------------------------\n\r");

                button_pressed = false;
            }
        }

        /* Check if the SPI FIFO is not FULL, to continuously write data to the SGPIO bus */
        if (Cy_SCB_SPI_GetTxFifoStatus(SGPIO_INITIATOR_SPI_HW) & CY_SCB_SPI_TX_NOT_FULL)
        {
            /* Write a frame where all drives are active */
            Cy_SCB_SPI_Write(SGPIO_INITIATOR_SPI_HW, ALL_DRIVES_ACTIVE);
        }
    }
}

/* [] END OF FILE */
