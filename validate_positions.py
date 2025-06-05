import cv2
import numpy as np
import re
import sys
from typing import List, Tuple
import os

# Grid layout parameters - adjust these to match your screenshot
MARGIN_LEFT = 55    # Pixels from left edge
MARGIN_RIGHT = -5   # Pixels from right edge
MARGIN_TOP = 30     # Pixels from top edge
MARGIN_BOTTOM = 30  # Pixels from bottom edge

class Hold:
    def __init__(self, position: int, color: str):
        self.position = position
        self.color = color

    def get_rgb(self) -> Tuple[int, int, int]:
        color_map = {
            'Red': (0, 0, 255),
            'Green': (0, 255, 0),
            'Blue': (255, 0, 0),
            'Pink': (255, 0, 255),
            'Yellow': (0, 255, 255),
            'White': (255, 255, 255),
            'Black': (0, 0, 0)
        }
        return color_map.get(self.color, (128, 128, 128))  # Default to gray if color not found

def parse_climb_summary(text: str) -> List[Hold]:
    holds = []
    for line in text.strip().split('\n'):
        if line.startswith('Position'):
            match = re.match(r'Position (\d+): (\w+)', line)
            if match:
                position = int(match.group(1))
                color = match.group(2)
                holds.append(Hold(position, color))
    return holds

def position_to_coordinates(position: int) -> Tuple[int, int]:
    # Tension Board 2 has 17x17 hole pairs, so 34 x 34 grid
    # Position numbers start from bottom-left, snake up and down
    # Each column has 34 positions (17 pairs)
    col = (position - 1) // 34
    row_in_col = (position - 1) % 34
    
    # If column is even, count from bottom to top
    # If column is odd, count from top to bottom
    if col % 2 == 0:
        row = 33 - row_in_col  # Start from bottom (33) and go up
    else:
        row = row_in_col  # Start from top (0) and go down
        
    return col, row

def is_valid_position(position: int) -> bool:
    # Define valid positions based on Tension Board 2 layout
    col, row = position_to_coordinates(position)
    
    # Basic validation - adjust these ranges based on actual board layout
    if row < 0 or row >= 34 or col < 0 or col >= 34:
        return False
        
    # Add specific position validations here
    # For example, some positions might not exist in certain areas
    return True

def draw_holds(image: np.ndarray, holds: List[Hold], circle_radius: int = 20) -> np.ndarray:
    # Get image dimensions
    height, width = image.shape[:2]
    print(f"Image dimensions: {width}x{height}")
    
    # Calculate usable area
    usable_width = width - MARGIN_LEFT - MARGIN_RIGHT
    usable_height = height - MARGIN_TOP - MARGIN_BOTTOM
    
    # Calculate grid cell size
    cell_width = usable_width // 34
    cell_height = usable_height // 34
    print(f"Cell size: {cell_width}x{cell_height}")
    
    # Create a copy of the image to draw on
    result = image.copy()
    
    # Draw grid lines for debugging
    for i in range(35):
        # Vertical lines
        x = MARGIN_LEFT + i * cell_width
        cv2.line(result, (x, MARGIN_TOP), (x, height - MARGIN_BOTTOM), (128, 128, 128), 1)
        # Horizontal lines
        y = MARGIN_TOP + i * cell_height
        cv2.line(result, (MARGIN_LEFT, y), (width - MARGIN_RIGHT, y), (128, 128, 128), 1)
    
    for hold in holds:
        if not is_valid_position(hold.position):
            print(f"Warning: Position {hold.position} is not valid on the board")
            continue
            
        col, row = position_to_coordinates(hold.position)
        print(f"Position {hold.position} -> Grid coordinates: ({col}, {row})")
        
        # Calculate center of the circle
        center_x = MARGIN_LEFT + int((col + 0.5) * cell_width)
        center_y = MARGIN_TOP + int((row + 0.5) * cell_height)
        print(f"Circle center: ({center_x}, {center_y})")
        
        # Draw the circle with a black outline
        cv2.circle(result, (center_x, center_y), circle_radius, (0, 0, 0), 2)  # Black outline
        cv2.circle(result, (center_x, center_y), circle_radius - 2, hold.get_rgb(), -1)  # Filled circle
        
        # Add position number with a black background for better visibility
        text = str(hold.position)
        text_size = cv2.getTextSize(text, cv2.FONT_HERSHEY_SIMPLEX, 0.5, 1)[0]
        text_x = center_x - text_size[0] // 2
        text_y = center_y + text_size[1] // 2
        
        # Draw black background for text
        cv2.rectangle(result, 
                     (text_x - 2, text_y - text_size[1] - 2),
                     (text_x + text_size[0] + 2, text_y + 2),
                     (0, 0, 0), -1)
        
        # Draw white text
        cv2.putText(result, text, (text_x, text_y),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1)
    
    return result

def main():
    if len(sys.argv) != 3:
        print("Usage: python validate_positions.py <screenshot_path> <climb_summary_text>")
        print("Example: python validate_positions.py screenshot.jpg 'Position 265: Pink\\nPosition 213: Blue'")
        sys.exit(1)
    
    # Read the screenshot
    image = cv2.imread(sys.argv[1])
    if image is None:
        print(f"Error: Could not read image {sys.argv[1]}")
        sys.exit(1)
    
    # Parse the climb summary
    holds = parse_climb_summary(sys.argv[2])
    print(f"Parsed {len(holds)} holds:")
    for hold in holds:
        print(f"Position {hold.position}: {hold.color}")
    
    # Draw the holds on the image
    result = draw_holds(image, holds)
    
    # Save the result
    basepath = os.path.dirname(sys.argv[1])
    basename = os.path.basename(sys.argv[1])
    output_path = os.path.join(basepath, "validated_" + basename)
    cv2.imwrite(output_path, result)
    print(f"Saved validated image to {output_path}")
    
    # Show the result
    cv2.imshow("Validated Positions", result)
    cv2.waitKey(0)
    cv2.destroyAllWindows()

if __name__ == "__main__":
    main() 