import cv from './opencv11'
import config from './config';
import * as smoothed_z_score from '@joe_six/smoothed-z-score-peak-signal-detection'

const anchor = new cv.Point(-1, -1);

export async function visualize(matIn, matOut) {
    let matTmp = new cv.Mat();
    matIn.convertTo(matTmp, cv.CV_8UC1, 0.01, 0);

    cv.medianBlur(matTmp, matTmp, 3)
    cv.rotate(matTmp, matTmp, cv.ROTATE_90_COUNTERCLOCKWISE)

    let matTop = matTmp.rowRange(0, 90);
    matTop.convertTo(matTop, cv.CV_8UC1, 0.5, 0);
    
    let matBottom = matTmp.rowRange(90, 180);
    matBottom.convertTo(matBottom, cv.CV_8UC1, 0.5, 0);

    let matMiddle = new cv.Mat(matTop.rows, matTmp.cols, cv.CV_8UC1, [128, 0, 0, 0]);
    matMiddle.setTo([128, 0, 0, 0])

    cv.add(matTop, matMiddle, matMiddle);
    cv.subtract(matMiddle, matBottom, matMiddle);

    cv.applyColorMap(matMiddle, matMiddle, 2);
    cv.cvtColor(matMiddle, matOut, cv.COLOR_BGR2RGB);

    matTmp.delete();
    matMiddle.delete();
}

function stage1(mat) {
    let matTmp = new cv.Mat();
    mat.convertTo(matTmp, cv.CV_8UC1, 0.01, 0);

    cv.medianBlur(matTmp, matTmp, 3)
    cv.rotate(matTmp, matTmp, cv.ROTATE_90_COUNTERCLOCKWISE)

    return matTmp;
}

function stage2(mat) {
    cv.threshold(mat, mat, 15, 255, cv.THRESH_BINARY)
    let matTop = mat.rowRange(0, 90);
    let matBottom = mat.rowRange(90, 180);
    
    let im2 = new cv.Mat(matTop.rows, matTop.cols, cv.CV_8UC1, [0, 0, 0, 0]);
    let im3 = new cv.Mat(matTop.rows, matTop.cols, cv.CV_8UC1, [0, 0, 0, 0]);

    cv.min(
        matTop.colRange(0, matTop.cols-2),
        matBottom.colRange(2, matBottom.cols),
        im2.colRange(0, im2.cols-2)
    )

    cv.min(
        matTop.colRange(2, matTop.cols),
        matBottom.colRange(0, matBottom.cols-2),
        im3.colRange(0, im2.cols-2)
    )

    function pre_filter(pre_mat) {
        cv.dilate(
            pre_mat,
            pre_mat,
            cv.Mat.ones(2, 2, cv.CV_8U),
            anchor,
            1,
            cv.BORDER_CONSTANT,
            cv.morphologyDefaultBorderValue()
        );

        cv.morphologyEx(
            pre_mat,
            pre_mat,
            cv.MORPH_CLOSE,
            cv.Mat.ones(8, 2, cv.CV_8U),
            anchor,
            2,
            cv.BORDER_CONSTANT,
            cv.morphologyDefaultBorderValue()
        );
        
        cv.dilate(
            pre_mat,
            pre_mat,
            cv.Mat.ones(2, 2, cv.CV_8U),
            anchor,
            1,
            cv.BORDER_CONSTANT,
            cv.morphologyDefaultBorderValue()
        );
    }

    pre_filter(im2)
    pre_filter(im3)

    mat.delete();

    return [im2, im3]
}

function stage3(mats) {
    function process_part(mat1in, mat2) {
        let mat1 = mat1in.clone();

        cv.threshold(mat1, mat1, 70, 255, cv.THRESH_BINARY);
        cv.dilate(
            mat1,
            mat1,
            cv.Mat.ones(4, 4, cv.CV_8U),
            anchor,
            1,
            cv.BORDER_CONSTANT,
            cv.morphologyDefaultBorderValue()
        );
        cv.morphologyEx(
            mat1,
            mat1,
            cv.MORPH_CLOSE,
            cv.Mat.ones(3, 8, cv.CV_8U),
            anchor,
            4,
            cv.BORDER_CONSTANT,
            cv.morphologyDefaultBorderValue()
        );

        cv.bitwise_and(mat2, mat2, mat1, mat1);
        cv.threshold(mat1, mat1, 5, 255, cv.THRESH_BINARY);
        cv.dilate(
            mat1,
            mat1,
            cv.Mat.ones(4, 2, cv.CV_8U),
            anchor,
            1,
            cv.BORDER_CONSTANT,
            cv.morphologyDefaultBorderValue()
        );
        cv.medianBlur(mat1, mat1, 3);

        return mat1;
    }

    let mat1 = process_part(mats[0], mats[1]);
    let mat2 = process_part(mats[1], mats[0]);

    mats[0].delete();
    mats[1].delete();

    return [mat1, mat2];
}

function stage4(mats) {
    let scores1 = new cv.Mat(1, mats[0].cols, cv.CV_64F);
    let scores2 = new cv.Mat(1, mats[0].cols, cv.CV_64F);
    let scores = new cv.Mat(1, mats[0].cols, cv.CV_64F);

    cv.reduce(mats[0], scores1, 0, cv.REDUCE_SUM, cv.CV_64F);
    cv.reduce(mats[1], scores2, 0, cv.REDUCE_SUM, cv.CV_64F);

    cv.subtract(scores1, scores2, scores);

    const filterKernel = cv.Mat.ones(1, 2, cv.CV_64F);
    cv.filter2D(scores, scores, cv.CV_64F, filterKernel, anchor, 0, cv.BORDER_DEFAULT);
    scores.convertTo(scores, cv.CV_64F, 1/2, 0);

    mats[0].delete();
    mats[1].delete();

    // let scoresArray = scores.data64F;

    return scores.data64F
}

export function stage5(scores) {
    let peaks = smoothed_z_score(scores, {
        lag: 3,
        threshold: 2,
        influence: 0.3
    })

    let beats = peaks.reduce((lastReturn, currentValue, currentIndex, sourceArray) => {
        if(currentValue == 0) 
            return lastReturn

        if(lastReturn.length > 0 && lastReturn[lastReturn.length-1].type == currentValue) {
            lastReturn[lastReturn.length-1].end = currentIndex
            lastReturn[lastReturn.length-1].values.push(scores[currentIndex])
            return lastReturn
        }

        lastReturn.push({
            start: currentIndex,
            end: currentIndex,
            type: currentValue,
            values: [scores[currentIndex]]
        })

        return lastReturn
    }, [])

    beats = beats.map(b => {
        const pos = parseInt((b.start + b.end) / 2)
        const val = b.values.reduce((a, b) => a + b, 0) / b.values.length
        
        return {
            pos: Math.max(0, pos),
            val,
            type: b.type
        }
    })

    return beats
}

export function process(mat) {
    mat = stage1(mat);
    mat = stage2(mat);
    mat = stage3(mat);
    mat = stage4(mat);
    // mat = stage5(mat);

    return mat;
}