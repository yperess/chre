/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package com.google.android.chre.test.crossvalidator;

import android.content.Context;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.hardware.location.ContextHubInfo;
import android.hardware.location.ContextHubManager;
import android.hardware.location.NanoAppBinary;
import android.hardware.location.NanoAppMessage;

import androidx.test.InstrumentationRegistry;

import com.google.android.chre.nanoapp.proto.ChreCrossValidation;
import com.google.android.utils.chre.ChreTestUtil;
import com.google.common.collect.BiMap;
import com.google.common.collect.HashBiMap;
import com.google.protobuf.InvalidProtocolBufferException;

import org.junit.Assert;
import org.junit.Assume;

import java.util.Arrays;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.TimeUnit;

public class ChreCrossValidatorSensor
        extends ChreCrossValidatorBase implements SensorEventListener {
    /**
    * Contains settings that can be adjusted per senor.
    */
    private static class CrossValidatorSensorConfig {
        // The number of float values expected in the values array of a datapoint
        public final int expectedValuesLength;
        // The amount that each value in the values array of a certain datapoint can differ between
        // AP and CHRE
        public final float errorMargin;

        CrossValidatorSensorConfig(int expectedValuesLength, float errorMargin) {
            this.expectedValuesLength = expectedValuesLength;
            this.errorMargin = errorMargin;
        }
    }

    private static final long AWAIT_DATA_TIMEOUT_CONTINUOUS_IN_MS = 5000;
    private static final long AWAIT_DATA_TIMEOUT_ON_CHANGE_ONE_SHOT_IN_MS = 1000;

    private static final long DEFAULT_SAMPLING_INTERVAL_IN_MS = 20;

    // TODO(b/146052784): May need to account for differences in sampling rate and latency from
    // AP side vs CHRE side
    private static final long SAMPLING_LATENCY_IN_MS = 0;

    private static final long MAX_TIMESTAMP_DIFF_NS = 10000000L;

    private ConcurrentLinkedQueue<ApSensorDatapoint> mApDatapointsQueue;
    private ConcurrentLinkedQueue<ChreSensorDatapoint> mChreDatapointsQueue;

    private ApSensorDatapoint[] mApDatapointsArray;
    private ChreSensorDatapoint[] mChreDatapointsArray;

    private SensorManager mSensorManager;
    private Sensor mSensor;

    private long mSamplingIntervalInMs;

    private CrossValidatorSensorConfig mSensorConfig;

    private static final BiMap<Integer, Integer> AP_TO_CHRE_SENSOR_TYPE =
            makeApToChreSensorTypeMap();
    private static final Map<Integer, CrossValidatorSensorConfig> SENSOR_TYPE_TO_CONFIG =
            makeSensorTypeToInfoMap();

    /*
    * @param contextHubManager The context hub manager that will be passed to super ctor.
    * @param contextHubInfo The context hub info that will be passed to super ctor.
    * @param nappAppBinary The nanoapp binary that will be passed to super ctor.
    * @param apSensorType The sensor type that this sensor validator will validate against. This
    *     must be one of the int constants starting with TYPE_ defined in android.hardware.Sensor
    *     class.
    */
    public ChreCrossValidatorSensor(ContextHubManager contextHubManager,
            ContextHubInfo contextHubInfo, NanoAppBinary nanoAppBinary, int apSensorType)
            throws AssertionError {
        super(contextHubManager, contextHubInfo, nanoAppBinary);
        mApDatapointsQueue = new ConcurrentLinkedQueue<ApSensorDatapoint>();
        mChreDatapointsQueue = new ConcurrentLinkedQueue<ChreSensorDatapoint>();
        Assert.assertTrue(String.format("Sensor type %d is not recognized", apSensorType),
                isSensorTypeValid(apSensorType));
        mSensorConfig = SENSOR_TYPE_TO_CONFIG.get(apSensorType);
        mSensorManager =
                (SensorManager) InstrumentationRegistry.getInstrumentation().getContext()
                .getSystemService(Context.SENSOR_SERVICE);
        Assert.assertNotNull("Sensor manager could not be instantiated.", mSensorManager);
        mSensor = mSensorManager.getDefaultSensor(apSensorType);
        Assume.assumeNotNull(String.format("Sensor could not be instantiated for sensor type %d.",
                apSensorType),
                mSensor);
        mSamplingIntervalInMs =
                Math.min(Math.max(
                        DEFAULT_SAMPLING_INTERVAL_IN_MS,
                        TimeUnit.MICROSECONDS.toMillis(mSensor.getMinDelay())),
                TimeUnit.MICROSECONDS.toMillis(mSensor.getMaxDelay()));
    }

    @Override
    protected NanoAppMessage makeStartNanoAppMessage() {
        int messageType = ChreCrossValidation.MessageType.CHRE_CROSS_VALIDATION_START_VALUE;
        ChreCrossValidation.StartSensorCommand startSensor =
                ChreCrossValidation.StartSensorCommand.newBuilder()
                .setSamplingIntervalInNs(TimeUnit.MILLISECONDS.toNanos(
                          mSamplingIntervalInMs))
                .setSamplingMaxLatencyInNs(TimeUnit.MILLISECONDS.toNanos(SAMPLING_LATENCY_IN_MS))
                .setChreSensorType(getChreSensorType())
                .setIsContinuous(sensorIsContinuous())
                .build();
        ChreCrossValidation.StartCommand startCommand =
                ChreCrossValidation.StartCommand.newBuilder().setStartSensorCommand(startSensor)
                .build();
        return NanoAppMessage.createMessageToNanoApp(
                mNappBinary.getNanoAppId(), messageType, startCommand.toByteArray());
    }

    @Override
    protected void parseDataFromNanoAppMessage(NanoAppMessage message) {
        final String kParseDataErrorPrefix = "While parsing data from nanoapp: ";
        ChreCrossValidation.Data dataProto;
        try {
            dataProto = ChreCrossValidation.Data.parseFrom(message.getMessageBody());
        } catch (InvalidProtocolBufferException e) {
            setErrorStr("Error parsing protobuff: " + e);
            return;
        }
        if (!dataProto.hasSensorData()) {
            setErrorStr(kParseDataErrorPrefix + "found non sensor type data");
        } else {
            ChreCrossValidation.SensorData sensorData = dataProto.getSensorData();
            int sensorType = chreToApSensorType(sensorData.getChreSensorType());
            if (!isSensorTypeCurrent(sensorType)) {
                setErrorStr(
                        String.format(kParseDataErrorPrefix
                        + "incorrect sensor type %d when expecting %d",
                        sensorType, mSensor.getType()));
            } else {
                for (ChreCrossValidation.SensorDatapoint datapoint :
                        sensorData.getDatapointsList()) {
                    int valuesLength = datapoint.getValuesList().size();
                    if (valuesLength != mSensorConfig.expectedValuesLength) {
                        setErrorStr(String.format(kParseDataErrorPrefix
                                + "incorrect sensor datapoints values length %d when expecing %d",
                                sensorType, valuesLength, mSensorConfig.expectedValuesLength));
                        break;
                    }
                    mChreDatapointsQueue.add(new ChreSensorDatapoint(datapoint));
                }
            }
        }
    }

    @Override
    protected void registerApDataListener() {
        Assert.assertTrue(mSensorManager.registerListener(
                this, mSensor, (int) TimeUnit.MILLISECONDS.toMicros(
                mSamplingIntervalInMs)));
    }

    @Override
    protected void unregisterApDataListener() {
        mSensorManager.unregisterListener(this);
    }

    @Override
    protected void assertApAndChreDataSimilar() throws AssertionError {
        // Copy concurrent queues to arrays so that other threads will not mutate the data being
        // worked on
        mApDatapointsArray = mApDatapointsQueue.toArray(new ApSensorDatapoint[0]);
        mChreDatapointsArray = mChreDatapointsQueue.toArray(new ChreSensorDatapoint[0]);
        Assert.assertTrue("Did not find any CHRE datapoints", mChreDatapointsArray.length > 0);
        Assert.assertTrue("Did not find any AP datapoints", mApDatapointsArray.length > 0);
        alignApAndChreDatapoints();
        // AP and CHRE datapoints will be same size
        // TODO(b/146052784): Ensure that CHRE data is the same sampling rate as AP data for
        // comparison
        for (int i = 0; i < mApDatapointsArray.length; i++) {
            assertSensorDatapointsSimilar(
                    (ApSensorDatapoint) mApDatapointsArray[i],
                    (ChreSensorDatapoint) mChreDatapointsArray[i], i);
        }
    }

    @Override
    protected long getAwaitDataTimeoutInMs() {
        if (mSensor.getType() == Sensor.REPORTING_MODE_CONTINUOUS) {
            return AWAIT_DATA_TIMEOUT_CONTINUOUS_IN_MS;
        } else {
            return AWAIT_DATA_TIMEOUT_ON_CHANGE_ONE_SHOT_IN_MS;
        }
    }

    @Override
    public void onAccuracyChanged(Sensor sensor, int accuracy) {}

    @Override
    public void onSensorChanged(SensorEvent event) {
        if (mCollectingData.get()) {
            int sensorType = event.sensor.getType();
            if (!isSensorTypeCurrent(sensorType)) {
                setErrorStr(String.format("incorrect sensor type %d when expecting %d",
                                          sensorType, mSensor.getType()));
            } else {
                mApDatapointsQueue.add(new ApSensorDatapoint(event));
            }
        }
    }

    @Override
    public void init() throws AssertionError {
        super.init();
        restrictSensors();
    }

    @Override
    public void deinit() throws AssertionError {
        super.deinit();
        unrestrictSensors();
    }

    /*
    * @param sensorType The sensor type that was passed to the ctor that will be validated.
    * @return true if sensor type is recognized.
    */
    private static boolean isSensorTypeValid(int sensorType) {
        return SENSOR_TYPE_TO_CONFIG.containsKey(sensorType);
    }

    /**
     * @param sensorType The sensor type received from nanoapp or Android framework.
     * @return true if sensor type matches current sensor type expected.
     */
    private boolean isSensorTypeCurrent(int sensorType) {
        return sensorType == mSensor.getType();
    }

    /**
    * Make the sensor type info objects for each sensor type and map from sensor type to those
    * objects.
    *
    * @return The map from sensor type to info for that type.
    */
    private static Map<Integer, CrossValidatorSensorConfig> makeSensorTypeToInfoMap() {
        Map<Integer, CrossValidatorSensorConfig> map =
                new HashMap<Integer, CrossValidatorSensorConfig>();
        // new CrossValidatorSensorConfig(<expectedValuesLength>, <errorMargin>)
        map.put(Sensor.TYPE_ACCELEROMETER, new CrossValidatorSensorConfig(3, 0.01f));
        map.put(Sensor.TYPE_GYROSCOPE, new CrossValidatorSensorConfig(3, 0.01f));
        map.put(Sensor.TYPE_MAGNETIC_FIELD, new CrossValidatorSensorConfig(3, 0.01f));
        map.put(Sensor.TYPE_PRESSURE, new CrossValidatorSensorConfig(1, 0.01f));
        map.put(Sensor.TYPE_LIGHT, new CrossValidatorSensorConfig(1, 0.01f));
        return map;
    }

    /**
    * Make the map from CHRE sensor type values to their AP values.
    *
    * @return The map from sensor type to info for that type.
    */
    private static BiMap<Integer, Integer> makeApToChreSensorTypeMap() {
        BiMap<Integer, Integer> map = HashBiMap.create(4);
        // CHRE sensor type constants in //system/chre/chre_api/include/chre_api/chre/sensor_types.h
        map.put(Sensor.TYPE_ACCELEROMETER, 1 /* CHRE_SENSOR_TYPE_ACCELEROMETER */);
        map.put(Sensor.TYPE_GYROSCOPE, 6 /* CHRE_SENSOR_TYPE_GYROSCOPE */);
        map.put(Sensor.TYPE_MAGNETIC_FIELD, 8 /* CHRE_SENSOR_TYPE_MAGNETIC_FIELD */);
        map.put(Sensor.TYPE_PRESSURE, 10 /* CHRE_SENSOR_TYPE_PRESSURE */);
        map.put(Sensor.TYPE_LIGHT, 12 /* CHRE_SENSOR_TYPE_LIGHT */);
        return map;
    }

    /*
    * Align the AP and CHRE datapoints by finding the first pair that are similar comparing
    * linearly from there. Also truncate the end if one list has more datapoints than the other
    * after this. This is needed because AP and CHRE can start sending data and varying times to
    * this validator and can also stop sending at various times.
    */
    private void alignApAndChreDatapoints() throws AssertionError {
        int matchAp = 0, matchChre = 0;
        int shorterDpLength = Math.min(mApDatapointsArray.length, mChreDatapointsArray.length);
        if (mApDatapointsArray[0].timestamp < mChreDatapointsArray[0].timestamp) {
            matchChre = 0;
            matchAp = indexOfFirstClosestDatapoint((SensorDatapoint[]) mApDatapointsArray,
                                                   shorterDpLength, mChreDatapointsArray[0]);
        } else {
            matchAp = 0;
            matchChre = indexOfFirstClosestDatapoint((SensorDatapoint[]) mChreDatapointsArray,
                                                     shorterDpLength, mApDatapointsArray[0]);
        }
        Assert.assertTrue("Did not find matching timestamps to align AP and CHRE datapoints.",
                          (matchAp != -1 && matchChre != -1));
        // Remove extraneous datapoints before matching datapoints
        int apStartI = matchAp;
        int chreStartI = matchChre;
        int newApLength = mApDatapointsArray.length - apStartI;
        int newChreLength = mChreDatapointsArray.length - chreStartI;
        int chreEndI = chreStartI + Math.min(newApLength, newChreLength);
        int apEndI = apStartI + Math.min(newApLength, newChreLength);
        mApDatapointsArray = Arrays.copyOfRange(mApDatapointsArray, apStartI, apEndI);
        mChreDatapointsArray = Arrays.copyOfRange(mChreDatapointsArray, chreStartI, chreEndI);
    }

    /**
    * Restrict other applications from accessing sensors. Should be called before validating data.
    */
    private void restrictSensors() {
        ChreTestUtil.executeShellCommand(InstrumentationRegistry.getInstrumentation(),
                "dumpsys sensorservice restrict ChreCrossValidatorSensor");
    }

    /**
    * Unrestrict other applications from accessing sensors. Should be called after validating data.
    */
    private void unrestrictSensors() {
        ChreTestUtil.executeShellCommand(
                InstrumentationRegistry.getInstrumentation(), "dumpsys sensorservice enable");
    }

    /**
     * Helper method for asserting a single pair of AP and CHRE datapoints are similar.
     */
    private void assertSensorDatapointsSimilar(ApSensorDatapoint apDp,
                                               ChreSensorDatapoint chreDp, int index) {
        String datapointsAssertMsg =
                String.format("AP and CHRE three axis datapoint values differ on index %d", index)
                + "\nAP data -> " + apDp + "\nCHRE data -> "
                + chreDp;
        String timestampsAssertMsg =
                String.format("AP and CHRE three axis timestamp values differ on index %d", index)
                + "\nAP data -> " + apDp + "\nCHRE data -> "
                + chreDp;

        // TODO(b/146052784): Log full list of datapoints to file on disk on assertion failure
        // so that there is more insight into the problem then just logging the one pair of
        // datapoints
        Assert.assertTrue(datapointsAssertMsg,
                datapointValuesAreSimilar(
                apDp, chreDp, mSensorConfig.errorMargin));
        Assert.assertTrue(timestampsAssertMsg,
                datapointTimestampsAreSimilar(apDp, chreDp));
    }

    /**
     * @param datapoints Array of dataoints to compare timestamps to laterDp
     * @param shorterLength Length of shorter datapoints array
     * @param laterDp SensorDatapoint whose timestamp will be compared to the datapoints in array
     *    to find the first pair that match.
     */
    private int indexOfFirstClosestDatapoint(SensorDatapoint[] sensorDatapoints, int shorterLength,
                                             SensorDatapoint laterDp) {
        for (int i = 0; i < shorterLength; i++) {
            if (datapointTimestampsAreSimilar(sensorDatapoints[i], laterDp)) {
                return i;
            }
        }
        return -1;
    }

    /**
     * @param chreSensorType The CHRE sensor type value.
     *
     * @return The AP sensor type value.
     */
    private static int chreToApSensorType(int chreSensorType) {
        return AP_TO_CHRE_SENSOR_TYPE.inverse().get(chreSensorType);
    }

    /**
     * @return The CHRE sensor type of the sensor being validated.
     */
    private int getChreSensorType() {
        return AP_TO_CHRE_SENSOR_TYPE.get(mSensor.getType());
    }

    private boolean sensorIsContinuous() {
        return mSensor.getReportingMode() == Sensor.REPORTING_MODE_CONTINUOUS;
    }

    /*
     * @param apDp The AP sensor datapoint object to compare.
     * @param chreDp The CHRE sensor datapoint object to compare.
     *
     * @return true if timestamps are similar.
     */
    private static boolean datapointTimestampsAreSimilar(SensorDatapoint apDp,
                                                         SensorDatapoint chreDp) {
        return Math.abs(apDp.timestamp - chreDp.timestamp) < MAX_TIMESTAMP_DIFF_NS;
    }

    /*
     * @param apDp The AP SensorDatapoint object to compare.
     * @param chreDp The CHRE SensorDatapoint object to compare.
     * @param errorMargin The amount that each value in values array can differ between the two
     *     datapoints.
     * @return true if the datapoint values are all similar.
     */
    private static boolean datapointValuesAreSimilar(
            ApSensorDatapoint apDp, ChreSensorDatapoint chreDp, float errorMargin) {
        Assert.assertEquals(apDp.values.length, chreDp.values.length);
        for (int i = 0; i < apDp.values.length; i++) {
            float val1 = apDp.values[i];
            float val2 = chreDp.values[i];
            float diff = Math.abs(val1 - val2);
            if (diff > errorMargin) {
                return false;
            }
        }
        return true;
    }
}
