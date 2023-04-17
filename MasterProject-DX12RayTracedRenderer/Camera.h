#pragma once
#include "Utility.h"

class Camera
{
public:
	Camera(DirectX::XMFLOAT3 pos = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f), DirectX::XMFLOAT3 rot = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f), float fov = DirectX::XM_PI * 0.25f, float ratio = 16.0f / 9.0f, float nearClip = 0.01f, float farClip = 10000.0f) :
		mPos(pos), mFovY(fov), mAspect(ratio), mNearZ(nearClip), mFarZ(farClip) {
		DirectX::XMVECTOR normRot = DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&rot));
		DirectX::XMVECTOR tempPos = DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&pos), DirectX::XMVectorScale(normRot, 2.0f ));
		DirectX::XMFLOAT3 TempPos; 
		DirectX::XMStoreFloat3(&TempPos, tempPos);
		DirectX::XMFLOAT3 up = { 0.0f, 1.0f, 0.0f };
		LookAt(pos, TempPos, up);
		UpdateViewMatrix();
	}
	~Camera(){}

	DirectX::XMFLOAT3 GetPos() { return mPos; }
	void SetPos(DirectX::XMFLOAT3& newPos) { 
		mPos = newPos;
		bViewDirty = true;
	}

	const DirectX::XMFLOAT3 GetRightVector() { return mRight; }
	const DirectX::XMFLOAT3 GetForwardVector() { return mForward; }
	const DirectX::XMFLOAT3 GetUpVector() { return mUp; }

	void SetFrustrum(float fovY, float aspect, float zn, float zf) {
		mFovY = fovY;
		mAspect = aspect;
		mNearZ = zn;
		mFarZ = zf;

		mNearWinHeight = 2.0f * mNearZ * tanf(0.5f * mFovY);
		mFarWinHeight = 2.0f * mFarZ * tanf(0.5f * mFovY);

		DirectX::XMMATRIX proj = DirectX::XMMatrixPerspectiveFovLH(mFovY, mAspect, mNearZ, mFarZ);
		DirectX::XMStoreFloat4x4(&mProjMatrix, proj);
	}

	float GetNearZ() { return mNearZ; }
	float GetFarZ() { return mFarZ; }
	float GetAspect() { return mAspect; }
	float GetFovX() {
		float halfWidth = 0.5f * GetNearWinWidth();
		return 2.0f * atan(halfWidth / mNearZ);
	}
	float GetFovY() { return mFovY; }
	
	float GetNearWinHeight() { return mNearWinHeight; }
	float GetNearWinWidth() { return mAspect * mNearWinHeight; }
	float GetFarWinHeight() { return mFarWinHeight; }
	float GetFarWinWidth() { return mAspect * mFarWinHeight; }

	//mEyePos.x = mRadius * sinf(mPhi) * cosf(mTheta);
	//mEyePos.z = mRadius * sinf(mPhi) * sinf(mTheta);
	//mEyePos.y = mRadius * cosf(mPhi);

	//DirectX::XMVECTOR pos = DirectX::XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
	//DirectX::XMVECTOR target = DirectX::XMVectorZero();
	//DirectX::XMVECTOR up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	//DirectX::XMMATRIX view = DirectX::XMMatrixLookAtLH(pos, target, up);
	//DirectX::XMStoreFloat4x4(&mView, view);

	void LookAt(DirectX::XMFLOAT3& pos, DirectX::XMFLOAT3& target, DirectX::XMFLOAT3& up) {
		bViewDirty = true;

		DirectX::XMVECTOR Pos = DirectX::XMLoadFloat3(&pos);
		DirectX::XMVECTOR Target = DirectX::XMLoadFloat3(&target);
		DirectX::XMVECTOR Up = DirectX::XMLoadFloat3(&up);
		DirectX::XMVECTOR f = DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(Target, Pos));
		DirectX::XMVECTOR r = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(Up, f));
		DirectX::XMVECTOR u = DirectX::XMVector3Cross(f, r);

		DirectX::XMStoreFloat3(&mPos, Pos);
		DirectX::XMStoreFloat3(&mForward, f);
		DirectX::XMStoreFloat3(&mRight, r);
		DirectX::XMStoreFloat3(&mUp, u);
	}

	DirectX::XMMATRIX GetViewMatrix() { return DirectX::XMLoadFloat4x4(&mViewMatrix); }
	DirectX::XMMATRIX GetProjMatrix() { return DirectX::XMLoadFloat4x4(&mProjMatrix); }

	void MoveForwards(float dist) {
		DirectX::XMFLOAT3 temp = {dist * mForward.x, dist * mForward.y, dist * mForward.z};
		mPos = { mPos.x + temp.x, mPos.y + temp.y, mPos.z + temp.z };
		DirectX::XMVECTOR s = DirectX::XMVectorReplicate(dist);
		DirectX::XMVECTOR l = DirectX::XMLoadFloat3(&mForward);
		DirectX::XMVECTOR p = DirectX::XMLoadFloat3(&mPos);
		DirectX::XMStoreFloat3(&mPos, DirectX::XMVectorMultiplyAdd(s, l, p));
		bViewDirty = true;
	}
	void MoveRight(float dist) {
		DirectX::XMFLOAT3 temp = { dist * mRight.x, dist * mRight.y, dist * mRight.z };
		mPos = { mPos.x + temp.x, mPos.y + temp.y, mPos.z + temp.z };
		DirectX::XMVECTOR s = DirectX::XMVectorReplicate(dist);
		DirectX::XMVECTOR r = DirectX::XMLoadFloat3(&mRight);
		DirectX::XMVECTOR p = DirectX::XMLoadFloat3(&mPos);
		DirectX::XMStoreFloat3(&mPos, DirectX::XMVectorMultiplyAdd(s, r, p));
		bViewDirty = true;
	}
	void Pitch(float angle) {
		DirectX::XMMATRIX r = DirectX::XMMatrixRotationAxis(DirectX::XMLoadFloat3(&mRight), angle);
		DirectX::XMStoreFloat3(&mUp, DirectX::XMVector3TransformNormal(DirectX::XMLoadFloat3(&mUp), r));
		DirectX::XMStoreFloat3(&mForward, DirectX::XMVector3TransformNormal(DirectX::XMLoadFloat3(&mForward), r));
		bViewDirty = true;
	}
	void Yaw(float angle) {
		DirectX::XMMATRIX r = DirectX::XMMatrixRotationY(angle);
		DirectX::XMStoreFloat3(&mRight, DirectX::XMVector3TransformNormal(DirectX::XMLoadFloat3(&mRight), r));
		DirectX::XMStoreFloat3(&mUp, DirectX::XMVector3TransformNormal(DirectX::XMLoadFloat3(&mUp), r));
		DirectX::XMStoreFloat3(&mForward, DirectX::XMVector3TransformNormal(DirectX::XMLoadFloat3(&mForward), r));
		bViewDirty = true;
	}

	void UpdateViewMatrix() {
		if (bViewDirty) {
			DirectX::XMVECTOR r = DirectX::XMLoadFloat3(&mRight);
			DirectX::XMVECTOR u = DirectX::XMLoadFloat3(&mUp);
			DirectX::XMVECTOR f = DirectX::XMLoadFloat3(&mForward);
			DirectX::XMVECTOR p = DirectX::XMLoadFloat3(&mPos);

			f = DirectX::XMVector3Normalize(f);
			u = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(f, r));
			r = DirectX::XMVector3Cross(u, f);

			float x = -DirectX::XMVectorGetX(DirectX::XMVector3Dot(p, r));
			float y = -DirectX::XMVectorGetX(DirectX::XMVector3Dot(p, u));
			float z = -DirectX::XMVectorGetX(DirectX::XMVector3Dot(p, f));

			DirectX::XMStoreFloat3(&mRight, r);
			DirectX::XMStoreFloat3(&mUp, u);
			DirectX::XMStoreFloat3(&mForward, f);

			mViewMatrix(0, 0) = mRight.x;
			mViewMatrix(1, 0) = mRight.y;
			mViewMatrix(2, 0) = mRight.z;
			mViewMatrix(3, 0) = x;

			mViewMatrix(0, 1) = mUp.x;
			mViewMatrix(1, 1) = mUp.y;
			mViewMatrix(2, 1) = mUp.z;
			mViewMatrix(3, 1) = y;

			mViewMatrix(0, 2) = mForward.x;
			mViewMatrix(1, 2) = mForward.y;
			mViewMatrix(2, 2) = mForward.z;
			mViewMatrix(3, 2) = z;

			mViewMatrix(0, 3) = 0.0f;
			mViewMatrix(1, 3) = 0.0f;
			mViewMatrix(2, 3) = 0.0f;
			mViewMatrix(3, 3) = 1.0f;

			bViewDirty = false;
		}
	}

private:
	DirectX::XMFLOAT3 mPos = { 0.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT3 mRight = { 1.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT3 mUp = { 0.0f, 1.0f, 0.0f };
	DirectX::XMFLOAT3 mForward = { 0.0f, 0.0f, 1.0f };

	float mNearZ = 0.0f;
	float mFarZ = 0.0f;
	float mAspect = 0.0f;
	float mFovY = 0.0f;
	float mNearWinHeight = 0.0f;
	float mFarWinHeight = 0.0f;

	bool bViewDirty = true;

	DirectX::XMFLOAT4X4 mViewMatrix = IDENTITY_MATRIX;
	DirectX::XMFLOAT4X4 mProjMatrix = IDENTITY_MATRIX;
};

