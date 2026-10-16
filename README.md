<<<<<<< HEAD
# LAB 1 RC

## Testing the Project

The base code includes a virtual cable that allows you to test communication without initially needing two physical computers or a real RS-232 cable.

Run the following commands in a Linux terminal from the project directory.

### 1. Compile the project

```bash
make
```

This command compiles the application and the virtual cable.

### 2. Start the virtual cable

If `socat` is installed, run:

```bash
make run_cable
```

Keep this terminal open while testing.

### 3. Start the receiver

Open a second terminal in the project directory and run:

```bash
make run_rx
```

### 4. Start the transmitter

Open a third terminal in the project directory and run:

```bash
make run_tx
```

### 5. Check the received file

When the transfer is complete, run:

```bash
make check_files
```

This checks whether the received file is identical to the original file.
=======
# Redes-Computadores

Na pasta raiz do projeto:


Porta série:
- make run_rx

Porta série:
- make run_tx

Cable:
- run_cable
- on/off
>>>>>>> 1086c04 (Update README)
