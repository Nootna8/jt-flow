import cv2
import numpy as np
import scipy.signal as signal

# print("In the model!")

def fix_beat_groups(beats, scores):
    # Avarate out the peaks and valleys with multiple hits
    groups = []
    last_value = None
    group = []

    for b in beats:
        if last_value == b[2]:
            group.append(b)
            continue

        last_value = b[2]

        if len(group) > 0:
            groups.append(group)

        group = [b]

    if len(group) > 0:
        groups.append(group)

    beats_new = []
    for g in groups:
        center_x = int(np.mean([b[0] for b in g]))
        beats_new.append((center_x, scores[center_x], g[0][2]))

    return np.array(beats_new)

def scores_to_beats(scores):
    beats = []

    # Find peaks and valleys in the found scores
    peaks, _ = signal.find_peaks(scores, height=0, distance=6)
    valleys, _ = signal.find_peaks(scores * -1, height=0, distance=6)

    for p in peaks:
        beats.append((p, scores[p], 1))
    for p in valleys:
        beats.append((p, scores[p], -1))

    beats.sort(key=lambda x: x[0])
    return fix_beat_groups(beats, scores)

def stage1(image):
    image = cv2.rotate(image, cv2.ROTATE_90_COUNTERCLOCKWISE)
    image = cv2.medianBlur(image, 3)
    return image

def stage2(image):
    image = cv2.threshold(image, 15, 255, cv2.THRESH_BINARY)[1]
    
    im2 = np.min([  np.roll(image[:90], -2, 1), np.roll(image[90:], 2, 1)  ], axis=0)
    im3 = np.min([  np.roll(image[:90], 2, 1), np.roll(image[90:], -2, 1)  ], axis=0)

    im2d = (im2.astype(np.int16) - im3 ).clip(0, 255).astype(np.uint8)
    im3d = (im3.astype(np.int16) - im2 ).clip(0, 255).astype(np.uint8)

    def pre_filter(frame):
        frame = cv2.dilate(frame, np.ones((2,2),np.uint8), iterations = 1)
        frame = cv2.morphologyEx(frame, cv2.MORPH_CLOSE, np.ones((8,2),np.uint8), iterations = 2)
        frame = cv2.dilate(frame, np.ones((2,2),np.uint8), iterations = 1)    
        frame = np.roll(frame, -4, axis=1)
        return frame

    im2d = pre_filter(im2d)
    im3d = pre_filter(im3d)
    return [im2d, im3d]

def stage3(image):

    def process_part(frame1, frame2):
        frame1 = cv2.threshold(frame1, 70, 255, cv2.THRESH_BINARY)[1]
        frame1 = cv2.dilate(frame1, np.ones((4,4),np.uint8), iterations = 1)
        
        frame1closed = cv2.morphologyEx(frame1, cv2.MORPH_CLOSE, np.ones((3,8),np.uint8), iterations = 4)

        masked = cv2.bitwise_and(frame2, frame2, mask=frame1closed)
        masked = cv2.threshold(masked, 5, 255, cv2.THRESH_BINARY)[1]
        masked = cv2.dilate(masked, np.ones((4,2),np.uint8), iterations = 1)
        masked = cv2.medianBlur(masked, 3)
        
        return masked

    myframe1 = process_part(image[0], image[1])
    myframe2 = process_part(image[1], image[0])
    return [myframe1, myframe2]

def stage4(image):
    scores1 = np.sum(image[0], axis=0).astype(np.float64)
    scores2 = np.sum(image[1], axis=0).astype(np.float64)
    scores = (scores1 - scores2) / (255 * 2)

    return scores

def stage5(scores):
    return scores_to_beats(scores)

def normalize(input):
    # print("Normalizing input")

    input = input.astype(np.float32)
    max_value = int(1280 * 720 * 0.01)
    input = cv2.threshold(input, max_value, max_value, cv2.THRESH_TRUNC)[1]
    input = cv2.normalize(input, None, 0, 255, cv2.NORM_MINMAX)
    
    input = input.astype(np.uint8)
    return input

    # input = cv2.normalize(input, None, 0, 255, cv2.NORM_MINMAX, dtype=cv2.CV_8U)
    
    # return input

def process(input):
    # return np.array([[0,0]], dtype=np.int32)
    input = normalize(input)

    input = stage1(input)
    input = stage2(input)
    input = stage3(input)
    input = stage4(input)
    input = stage5(input)

    result = []
    for i in input:
        if i[2] == -1:
            result.append([int(i[0]), 10])
        if i[2] == 1:
            result.append([int(i[0]), 90])

    print("Detected {} actions".format(len(input)))
    
    return np.array(result, dtype=np.int32)