/*
 * Copyright 2013, Jérôme Duval, korli@users.berlios.de.
 * Distributed under the terms of the MIT License.
 */


#include "VirtioSCSIPrivate.h"

#include <new>
#include <stdlib.h>
#include <string.h>

#include <util/AutoLock.h>


const char *
get_feature_name(uint32 feature)
{
	switch (feature) {
		case VIRTIO_SCSI_F_INOUT:
			return "in out";
		case VIRTIO_SCSI_F_HOTPLUG:
			return "hotplug";
	}
	return NULL;
}


VirtioSCSIController::VirtioSCSIController(device_node *node)
	:
	fNode(node),
	fVirtio(NULL),
	fVirtioDevice(NULL),
	fStatus(B_NO_INIT),
	fRequest(NULL)
{
	CALLED();

	B_INITIALIZE_SPINLOCK(&fInterruptLock);
	fInterruptCondition.Init(this, "virtio scsi transfer");
	
	// get the Virtio device from our parent's parent
	device_node *parent = gDeviceManager->get_parent_node(node);
	device_node *virtioParent = gDeviceManager->get_parent_node(parent);
	gDeviceManager->put_node(parent);

	gDeviceManager->get_driver(virtioParent, (driver_module_info **)&fVirtio,
		(void **)&fVirtioDevice);
	gDeviceManager->put_node(virtioParent);

	fVirtio->negociate_features(fVirtioDevice,
		0 /*VIRTIO_SCSI_F_HOTPLUG*/,
		&fFeatures, &get_feature_name);

	fStatus = fVirtio->read_device_config(fVirtioDevice, 0, &fConfig,
		sizeof(struct virtio_scsi_config));
	if (fStatus != B_OK)
		return;

	fConfig.sense_size = VIRTIO_SCSI_SENSE_SIZE;
	fConfig.cdb_size = VIRTIO_SCSI_CDB_SIZE;

	fVirtio->write_device_config(fVirtioDevice,
		offsetof(struct virtio_scsi_config, sense_size), &fConfig.sense_size,
		sizeof(fConfig.sense_size));
	fVirtio->write_device_config(fVirtioDevice,
		offsetof(struct virtio_scsi_config, cdb_size), &fConfig.sense_size,
		sizeof(fConfig.cdb_size));

	fRequest = new(std::nothrow) VirtioSCSIRequest(true);
	if (fRequest == NULL) {
		fStatus = B_NO_MEMORY;
		return;
	}

	::virtio_queue virtioQueues[3];
	fStatus = fVirtio->alloc_queues(fVirtioDevice, 3, virtioQueues);
	if (fStatus != B_OK) {
		ERROR("queue allocation failed (%s)\n", strerror(fStatus));
		return;
	}

	fControlVirtioQueue = virtioQueues[0];
	fEventVirtioQueue = virtioQueues[1];
	fRequestVirtioQueue = virtioQueues[2];
	
	fStatus = fVirtio->setup_interrupt(fVirtioDevice, NULL, NULL);
	if (fStatus != B_OK) {
		ERROR("interrupt setup failed (%s)\n", strerror(fStatus));
		return;
	}

	
}


VirtioSCSIController::~VirtioSCSIController()
{
	CALLED();
	delete fRequest;
}


status_t
VirtioSCSIController::InitCheck()
{
	return fStatus;
}


void
VirtioSCSIController::SetBus(scsi_bus bus)
{
	fBus = bus;
}


void
VirtioSCSIController::PathInquiry(scsi_path_inquiry *info)
{
	info->hba_inquiry = SCSI_PI_TAG_ABLE;
	info->hba_misc = 0;
	info->sim_priv = 0;
	info->initiator_id = VIRTIO_SCSI_INITIATOR_ID;
	info->hba_queue_size = fConfig.cmd_per_lun != 0 ? fConfig.cmd_per_lun : 1;
	memset(info->vuhba_flags, 0, sizeof(info->vuhba_flags));

	strlcpy(info->sim_vid, "Haiku", SCSI_SIM_ID);
	strlcpy(info->hba_vid, "VirtIO", SCSI_HBA_ID);

	strlcpy(info->sim_version, "1.0", SCSI_VERS);
	strlcpy(info->hba_version, "1.0", SCSI_VERS);
	strlcpy(info->controller_family, "Virtio", SCSI_FAM_ID);
	strlcpy(info->controller_type, "Virtio", SCSI_TYPE_ID);
}


void
VirtioSCSIController::GetRestrictions(uint8 targetID, bool *isATAPI,
	bool *noAutoSense, uint32 *maxBlocks)
{
	*isATAPI = false;
	*noAutoSense = true;	
	*maxBlocks = fConfig.cmd_per_lun;
}


uchar
VirtioSCSIController::ResetDevice(uchar targetID, uchar targetLUN)
{
	return SCSI_REQ_CMP;
}


status_t
VirtioSCSIController::ExecuteRequest(scsi_ccb *ccb)
{
	status_t result = fRequest->Start(ccb);
	if (result != B_OK)
		return result;

	if (ccb->cdb[0] == SCSI_OP_REQUEST_SENSE && fRequest->HasSense()) {
		TRACE("request sense\n");
		fRequest->RequestSense();
		fRequest->Finish(false);
		return B_OK;
	}

	if (ccb->target_id > fConfig.max_target) {
		ERROR("invalid target device\n");
		fRequest->SetStatus(SCSI_TID_INVALID);
		fRequest->Finish(false);
		return B_BAD_INDEX;
	}

	if (ccb->target_lun > fConfig.max_lun) {
		ERROR("invalid lun device\n");
		fRequest->SetStatus(SCSI_LUN_INVALID);
		fRequest->Finish(false);
		return B_BAD_INDEX;
	}

	if (ccb->cdb_length > VIRTIO_SCSI_CDB_SIZE) {
		fRequest->SetStatus(SCSI_REQ_INVALID);
		fRequest->Finish(false);
		return B_BAD_VALUE;
	}

	bool isOut = (ccb->flags & SCSI_DIR_MASK) == SCSI_DIR_OUT;
	bool isIn = (ccb->flags & SCSI_DIR_MASK) == SCSI_DIR_IN;

	// TODO check feature inout if request is bidirectional

	fRequest->SetTimeout(ccb->timeout > 0 ? ccb->timeout * 1000 * 1000
		: VIRTIO_SCSI_STANDARD_TIMEOUT);

	uint32 inCount = (isIn ? ccb->sg_count : 0) + 1;
	uint32 outCount = (isOut ? ccb->sg_count : 0) + 1;
	physical_entry entries[inCount + outCount];
	fRequest->FillRequest(inCount, outCount, entries);

	{
		InterruptsSpinLocker locker(fInterruptLock);
		fExpectsInterrupt = true;
		fInterruptCondition.Add(&fInterruptConditionEntry);
	}

	fVirtio->queue_request_v(fRequestVirtioQueue, entries,
		outCount, inCount, VirtioSCSIController::RequestCallback, this);

	result = fInterruptConditionEntry.Wait(B_RELATIVE_TIMEOUT,
		fRequest->Timeout());
	
	{
		InterruptsSpinLocker locker(fInterruptLock);
		fExpectsInterrupt = false;
	}

	if (result != B_OK)
		return result;

	return fRequest->Finish(false);
}


uchar
VirtioSCSIController::AbortRequest(scsi_ccb *request)			
{
	return SCSI_REQ_CMP;
}


uchar
VirtioSCSIController::TerminateRequest(scsi_ccb *request)
{
	return SCSI_REQ_CMP;
}


status_t
VirtioSCSIController::Control(uint8 targetID, uint32 op, void *buffer,
	size_t length)
{
	CALLED();	
	return B_DEV_INVALID_IOCTL;
}


void
VirtioSCSIController::RequestCallback(void* cookie)
{
	CALLED();
	VirtioSCSIController* controller = (VirtioSCSIController*)cookie;
	controller->_Interrupt();
}


void
VirtioSCSIController::_Interrupt()
{
	SpinLocker locker(fInterruptLock);
	fInterruptCondition.NotifyAll();
}
