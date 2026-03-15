const express = require('express');
const sqlite3 = require('sqlite3').verbose();
const app = express();
const port = 3000;

//connect the database
const db = new sqlite3.Database('./database.sqlite', (err) => {
    if (err) {
        console.log(`failed to connect to the database ${err.message}`)
    }
    else {
        console.log(`connected to the database`)
    }
});

//create the transaction table if not exist
db.run(`
    CREATE TABLE IF NOT EXISTS transactions (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        Phone_number TEXT NOT NULL,
        meter_number TEXT NOT NULL,
        token TEXT NOT NULL,
        amount INTEGER NOT NULL,
        status TEXT NOT NULL
    )
`)

app.use(express.urlencoded({ extended: false }))

app.post('/ussd', (req, res) => {
    // Read the data sent by the telecom
    const { sessionId, ServiceCode, phoneNumber, text } = req.body;

    let response = '';

    //if the text is empty that means user just dialed the code
    if (text === '') {
        response = `CON welcome to the ussd electricity service
        1.By Electricity Token
        2.Check Balance
        `
    }

    //if the user selected 1
    else if (text === '1') {
        response = `CON Please enter your Meter number`;
    }

    //if the user entered 
    else if (text.startsWith('1*')) {

        //get the mock up token
        let generatedToken = Math.random().toString().slice(2).padEnd(20, '0');

        //user amount 
        let amount = 20;

        let status = 'Unused';
        //get the meter number
        let meterNumber = text.split('*')[1];

        //insert into the Database
        db.run(
            'INSERT INTO transactions (Phone_number, meter_number, token, amount, status) VALUES (?,?,?,?,?)',
            [phoneNumber, meterNumber, generatedToken, amount, status],
            function (err) {
                if (err) {
                    console.error("Error saving transaction", err.message);
                } else {
                    console.log(`Transaction saved successfully with id ${this.lastID}`)
                }
            }
        );

        response = `END Thank you generating token for meter number ${meterNumber}. Token is ${generatedToken}
        You will receive an sms shortly`;
    }

    //if the user selected 2
    else if (text === '2') {
        response = 'END your balance is $50'
    }

    //if invalid option
    else {
        response = 'END Invalid option'
    }

    //send the response back to the telecom
    res.set('content-type', 'text/plain');
    res.send(response);
});


app.get('/', (req, res) => {
    res.send('Welcome to the USSD Electricty Backend');
});

//Route to view all transactions
app.get('/transactions', (req, res) => {
    db.all('SELECT * FROM transactions', [], (err, rows) => {
        if(err){
            res.status(500).json({error: err.message})
            return;
        }
        res.json(rows);
    })
});

//Esp32 route 
app.get('/api/tokens/:meterNumber', (req, res) => {
    const meterNumber = req.params.meterNumber;

    //Search the database for the token 
    db.get(
        `SELECT token FROM transactions WHERE meter_number = ? AND status = 'Unused' ORDER BY ID ASC LIMIT 1`,
        [meterNumber],
        (err, row) => {
            if(err){
                res.status(500).json({error:err.message})
                return;
            }
            if(row){
                //if token is found send it to the Esp32
                res.json({token:row.token, hasNewToken:true});
            } else {
                //if no token found, tell the esp32 to keep waiting
                res.json({hasNewToken:false})
            }
        }
    )
});

app.listen(port, () => {
    console.log(`Server is running on port ${port}`);
});