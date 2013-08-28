/*++

Copyright (c) Microsoft Corporation.  All rights reserved.

    THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY
    KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR
    PURPOSE.

Module Name:

    IsrDpc.c

Abstract:

    Contains routines related to interrupt and dpc handling.

Environment:

    Kernel mode

--*/

#include "precomp.h"

#include "IsrDpc.tmh"


NTSTATUS
HdmiInterruptCreate(
    IN PDEVICE_EXTENSION DevExt
    )
/*++
Routine Description:

    Configure and create the WDFINTERRUPT object.
    This routine is called by EvtDeviceAdd callback.

Arguments:

    DevExt      Pointer to our DEVICE_EXTENSION

Return Value:

    NTSTATUS code

--*/
{
    NTSTATUS                    status;
    WDF_INTERRUPT_CONFIG        InterruptConfig;

    WDF_INTERRUPT_CONFIG_INIT( &InterruptConfig,
                               HdmiEvtInterruptIsr,
                               HdmiEvtInterruptDpc );

    // InterruptConfig.EvtInterruptEnable  = PLxEvtInterruptEnable;
    // InterruptConfig.EvtInterruptDisable = PLxEvtInterruptDisable;

    // JOHNR: Enable testing of the DpcForIsr Synchronization
    InterruptConfig.AutomaticSerialization = TRUE;
    InterruptConfig.ShareVector = WdfFalse;
    //
    // Unlike WDM, framework driver should create interrupt object in EvtDeviceAdd and
    // let the framework do the resource parsing and registration of ISR with the kernel.
    // Framework connects the interrupt after invoking the EvtDeviceD0Entry callback
    // and disconnect before invoking EvtDeviceD0Exit. EvtInterruptEnable is called after
    // the interrupt interrupt is connected and EvtInterruptDisable before the interrupt is
    // disconnected.
    //
    status = WdfInterruptCreate( DevExt->Device,
                                 &InterruptConfig,
                                 WDF_NO_OBJECT_ATTRIBUTES,
                                 &DevExt->Interrupt );

    if( !NT_SUCCESS(status) ) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                    "WdfInterruptCreate failed: %!STATUS!", status);
    }

    return status;
}


BOOLEAN
HdmiEvtInterruptIsr(
    IN WDFINTERRUPT Interrupt,
    IN ULONG        MessageID
    )
/*++
Routine Description:

    Interrupt handler for this driver. Called at DIRQL level when the
    device or another device sharing the same interrupt line asserts
    the interrupt. The driver first checks the device to make sure whether
    this interrupt is generated by its device and if so clear the interrupt
    register to disable further generation of interrupts and queue a
    DPC to do other I/O work related to interrupt - such as reading
    the device memory, starting a DMA transaction, coping it to
    the request buffer and completing the request, etc.

Arguments:

    Interupt   - Handle to WDFINTERRUPT Object for this device.
    MessageID  - MSI message ID (always 0 in this configuration)

Return Value:

     TRUE   --  This device generated the interrupt.
     FALSE  --  This device did not generated this interrupt.

--*/
{
    PDEVICE_EXTENSION   devExt;
    BOOLEAN             isRecognized = TRUE;

    

    UNREFERENCED_PARAMETER(MessageID);

    //TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT,
    //            "--> PLxInterruptHandler");

    devExt  = HdmiGetDeviceContext(WdfInterruptGetDevice(Interrupt));

	//WdfRequestUnmarkCancelable(devExt->Request);
	
    WdfInterruptQueueDpcForIsr( devExt->Interrupt);

    //TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT,
    //            "<-- PLxInterruptHandler");

    return isRecognized;
}

VOID
HdmiEvtInterruptDpc(
    IN WDFINTERRUPT Interrupt,
    IN WDFDEVICE    Device
    )
/*++

Routine Description:

    DPC callback for ISR. Please note that on a multiprocessor system,
    you could have more than one DPCs running simulataneously on
    multiple processors. So if you are accesing any global resources
    make sure to synchrnonize the accesses with a spinlock.

Arguments:

    Interupt  - Handle to WDFINTERRUPT Object for this device.
    Device    - WDFDEVICE object passed to InterruptCreate

Return Value:

--*/
{
    NTSTATUS            status;
    PDEVICE_EXTENSION   devExt;
    WDFDMATRANSACTION   dmaTransaction;
    BOOLEAN transactionComplete;
    size_t length;

    UNREFERENCED_PARAMETER(Device);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_DPC, "--> EvtInterruptDpc");

    devExt  = HdmiGetDeviceContext(WdfInterruptGetDevice(Interrupt));
    
		dmaTransaction = devExt->WriteDmaTransaction;

     length = WdfDmaTransactionGetCurrentDmaTransferLength( dmaTransaction );

     if ((length == 0) && (devExt->DMAcompleted == 7))
     	{
     		return;
     	}

	/* if (length == 0)
	 {
		WdfDmaTransactionDmaCompletedWithLength( dmaTransaction,
                                                                   length,
                                                                   &status );	 
	 	HdmiWriteRequestComplete( dmaTransaction, status );
		devExt->DMAcompleted = 0;
		return;
 	}*/
   
    /*transactionComplete = WdfDmaTransactionDmaCompletedWithLength( dmaTransaction,
                                                                   devExt->PerDma[devExt->PerDmaCurrent-1].ByteCnt,
                                                                   &status );
    */

	//WdfRequestMarkCancelable(devExt->Request, HdmiEvtRequestCancel);
		
    /*WRITE_REGISTER_ULONG( (PULONG)&devExt->Regs->WriteCtr.CtrBit,
            0xffffffff );

    WRITE_REGISTER_ULONG( (PULONG)&devExt->Regs->WriteCtr.CtrBit,
                            0x00000 | devExt->DmaNumber );
  
    WRITE_REGISTER_ULONG( (PULONG) &devExt->Regs->WriteCtr.DescTableAddressHigh,
                            devExt->CommonBufferPA >> 32 );
 
    WRITE_REGISTER_ULONG( (PULONG) &devExt->Regs->WriteCtr.DescTableAddressLow,
                            devExt->CommonBufferPA & 0xffffffff );
 
    WRITE_REGISTER_ULONG( (PULONG) &devExt->Regs->WriteCtr.LastDesc,
                            devExt->DmaNumber - 1 );*/


	transactionComplete = WdfDmaTransactionDmaCompleted( dmaTransaction,
                                                         &status );	
    if (transactionComplete) 
    {
          //
          // Complete this DmaTransaction.
          //
          TraceEvents(TRACE_LEVEL_INFORMATION, DBG_DPC,
                                   "Completing Write request in the DpcForIsr");

			
			HdmiWriteRequestComplete( dmaTransaction, status );
	devExt->DMAcompleted = 7;

    }
   
    
    
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_DPC, "<-- EvtInterruptDpc");

    return;
}

NTSTATUS
PLxEvtInterruptEnable(
    IN WDFINTERRUPT Interrupt,
    IN WDFDEVICE    Device
    )
/*++

Routine Description:

    Called by the framework at DIRQL immediately after registering the ISR with the kernel
    by calling IoConnectInterrupt.

Return Value:

    NTSTATUS
--*/
{
    PDEVICE_EXTENSION  devExt;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT,
                "PLxEvtInterruptEnable: Interrupt 0x%p, Device 0x%p\n",
                Interrupt, Device);

    devExt  = HdmiGetDeviceContext(WdfInterruptGetDevice(Interrupt));

    return STATUS_SUCCESS;
}

NTSTATUS
PLxEvtInterruptDisable(
    IN WDFINTERRUPT Interrupt,
    IN WDFDEVICE    Device
    )
/*++

Routine Description:

    Called by the framework at DIRQL before Deregistering the ISR with the kernel
    by calling IoDisconnectInterrupt.

Return Value:

    NTSTATUS
--*/
{
    PDEVICE_EXTENSION  devExt;


    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT,
                "PLxEvtInterruptDisable: Interrupt 0x%p, Device 0x%p\n",
                Interrupt, Device);

    devExt  = HdmiGetDeviceContext(WdfInterruptGetDevice(Interrupt));

    return STATUS_SUCCESS;
}

