@@
typedef FpImageDeviceState;
@@
(
-FP_IMG_COLORS_INVERTED
+FP_IMAGE_COLORS_INVERTED
|
-FP_IMG_H_FLIPPED
+FP_IMAGE_H_FLIPPED
|
-FP_IMG_V_FLIPPED
+FP_IMAGE_V_FLIPPED
|
-FP_VERIFY_RETRY_TOO_SHORT
+FP_DEVICE_RETRY_TOO_SHORT
|
-FP_VERIFY_RETRY_CENTER_FINGER
+FP_DEVICE_RETRY_CENTER_FINGER
|
-FP_VERIFY_RETRY
+FP_DEVICE_RETRY
)

@@
@@
(
-enum fp_imgdev_state
+FpImageDeviceState
)